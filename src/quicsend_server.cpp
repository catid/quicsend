#include <quicsend_server.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Server

QuicSendServer::QuicSendServer(const QuicSendServerSettings& settings)
{
    DatagramCallback datagram_callback = [this](
        uint8_t* data,
        std::size_t bytes,
        const boost::asio::ip::udp::endpoint& peer_endpoint)
    {
        OnDatagram(data, bytes, peer_endpoint);
    };

    qs_ = std::make_shared<QuicheSocket>(
        io_context_,
        datagram_callback,
        settings.Port,
        settings.CertPath,
        settings.KeyPath);

    sender_ = std::make_shared<QuicheSender>(qs_);

    loop_thread_ = std::make_shared<std::thread>([this]() {
        io_context_.run();
        closed_ = true;
    });

    qs_->StartReceive();
}

QuicSendServer::~QuicSendServer() {
    mailbox_.Shutdown();
    io_context_.stop();
    JoinThread(loop_thread_);
}

void QuicSendServer::Close(uint64_t connection_id) {
}

int64_t QuicSendServer::Request(
    uint64_t connection_id,
    const std::string& path,
    BodyDataType type,
    const void* data,
    int bytes)
{

}

void QuicSendServer::Poll(
    OnConnectCallback on_connect,
    OnTimeoutCallback on_timeout,
    OnDataCallback on_data,
    int timeout_msec)
{

}

void QuicSendServer::OnDatagram(
    uint8_t* data,
    std::size_t bytes,
    const boost::asio::ip::udp::endpoint& peer_endpoint)
{
    uint8_t type = 0;
    uint32_t version = 0;
    ConnectionId scid, dcid, odcid;
    uint8_t token[MAX_TOKEN_LEN];
    size_t token_len = sizeof(token);

    int rc = quiche_header_info(data, bytes,
                                LOCAL_CONN_ID_LEN,
                                &version, &type,
                                scid.data(), &scid.Length,
                                dcid.data(), &dcid.Length,
                                token, &token_len);
    if (rc < 0) {
        LOG_ERROR() << "Failed to parse header: " << rc << " " << quiche_error_to_string(rc);
        return;
    }

    std::shared_ptr<QuicheConnection> conn_ptr = sender_->Find(dcid);
    if (!conn_ptr) {
        if (!quiche_version_is_supported(version)) {
            send_version_negotiation(scid, dcid, peer_endpoint);
            LOG_WARN() << "New connection: Unsupported version " << version; 
            return;
        }

        if (token_len == 0) {
            // We require a token to connect to avoid DDoS attacks
            send_retry(scid, dcid, peer_endpoint);
            return;
        }

        if (!read_token(token, token_len, 
                        peer_endpoint,
                        odcid)) {
            LOG_ERROR() << "Invalid address validation token";
            return;
        }

        conn_ptr = create_conn(dcid, odcid, peer_endpoint);
        if (!conn_ptr) {
            LOG_ERROR() << "Failed to create connection";
            return;
        }
    }

    conn_ptr->OnDatagram(data, bytes, peer_endpoint);
}

void QuicSendServer::send_version_negotiation(
    const ConnectionId& scid,
    const ConnectionId& dcid,
    const boost::asio::ip::udp::endpoint& peer_endpoint)
{
    auto buffer = qs_->allocator_.Allocate();

    ssize_t written = quiche_negotiate_version(
        scid.data(), scid.Length,
        dcid.data(), dcid.Length,
        buffer->Payload, sizeof(buffer->Payload));
    if (written < 0) {
        LOG_ERROR() << "Failed to create version negotiation packet: " << written << " " << quiche_error_to_string(written);
        return;
    }
    buffer->Length = written;

    qs_->Send(buffer, peer_endpoint);
}

void QuicSendServer::send_retry(
    const ConnectionId& scid,
    const ConnectionId& dcid,
    const boost::asio::ip::udp::endpoint& peer_endpoint)
{
    ConnectionId new_scid;
    new_scid.Randomize();

    auto token = mint_token(
        dcid,
        peer_endpoint);

    auto buffer = qs_->allocator_.Allocate();

    ssize_t written = quiche_retry(
        scid.data(), scid.Length,
        dcid.data(), dcid.Length,
        new_scid.data(), new_scid.Length,
        token.data(), token.size(),
        QUICHE_PROTOCOL_VERSION,
        buffer->Payload, sizeof(buffer->Payload));
    if (written < 0) {
        LOG_ERROR() << "Failed to create retry packet: " << written << " " << quiche_error_to_string(written);
        return;
    }
    buffer->Length = written;

    qs_->Send(buffer, peer_endpoint);
}

std::shared_ptr<QuicheConnection> QuicSendServer::create_conn(
    const ConnectionId& dcid,
    const ConnectionId& odcid,
    const boost::asio::ip::udp::endpoint& peer_endpoint)
{
    std::shared_ptr<QuicheConnection> qc = std::make_shared<QuicheConnection>();

    QCSettings settings;
    settings.AssignedId = ++next_assigned_id_;
    settings.qs = qs_;
    settings.dcid = dcid;
    settings.on_timeout = [this, dcid]() {
        LOG_INFO() << "*** Connection timeout";
    };
    settings.on_connect = [this]() {
        LOG_INFO() << "*** Connection established";
    };
    QuicheConnection* qcp = qc.get();
    settings.on_request = [this, qcp](DataStream& stream) {
        OnRequest(qcp, stream);
    };

    qc->Initialize(settings);
    if (!qc->Accept(peer_endpoint, dcid, odcid)) {
        return nullptr;
    }
    sender_->Add(dcid, qc);
    return qc;
}

void QuicSendServer::OnRequest(QuicheConnection* qcp, DataStream& stream) {
    LOG_INFO() << "*** [" << stream.Id << "] Requested " << stream.Method
        << ": " << stream.Path << " " << stream.Buffer->size() << " bytes";

    std::string data(1024 * 1024 * 1024, 'A');

    const std::vector<std::pair<std::string, std::string>> headers = {
        {":status", "200"},
        {"user-agent", "quiche"},
        {"content-type", "application/octet-stream"},
        {"content-length", std::to_string(data.size())}
    };

    qcp->SendResponse(stream.Id, headers, data.c_str(), data.size());
}
