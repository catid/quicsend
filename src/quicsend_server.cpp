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
    // FIXME
    auto conn = sender_->Find(ConnectionId{});  // You need to maintain a mapping from connection_id to ConnectionId
    if (conn) {
        conn->settings_.on_timeout(connection_id);
        // Remove the connection from the sender
        sender_->Remove(conn->settings_.dcid);
    }
}

int64_t QuicSendServer::Request(
    uint64_t connection_id,
    const std::string& path,
    BodyDataType type,
    const void* data,
    int bytes)
{
    // FIXME
    auto conn = sender_->Find(ConnectionId{});  // You need to maintain a mapping from connection_id to ConnectionId
    if (!conn) {
        return -1;
    }

    std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "GET"},
        {":path", path},
        {"user-agent", "quicsend"},
        {"content-type", BodyDataTypeToString(type)},
    };

    if (bytes > 0) {
        headers.push_back({"content-length", std::to_string(bytes)});
    }

    int64_t stream_id = conn->SendRequest(
        [this, connection_id](DataStream& stream) {
            QuicheMailbox::Event event;
            event.ConnectionId = connection_id;
            event.IsResponse = true;
            event.Id = stream.Id;
            event.Path = stream.Path;
            event.Type = stream.ContentType;
            event.Buffer = stream.Buffer;
            mailbox_.Post(event);
        },
        headers,
        data,
        bytes
    );

    return stream_id;
}

void QuicSendServer::Poll(
    OnConnectCallback on_connect,
    OnTimeoutCallback on_timeout,
    OnDataCallback on_data,
    int timeout_msec)
{
    // FIXME
    mailbox_.Poll([&](const QuicheMailbox::Event& event) {
        if (event.IsResponse) {
            on_data(event.ConnectionId, event);
        } else {
            // This is a new connection event
            on_connect(event.ConnectionId, 
                sender_->Find(ConnectionId{})->peer_endpoint_);  // You need to maintain a mapping
        }
    }, timeout_msec);

    // Check for timeouts
    auto connections = sender_->GetConnections();  // You need to add this method to QuicheSender
    for (const auto& pair : connections) {
        if (pair.second->IsClosed()) {
            on_timeout(pair.second->settings_.AssignedId);
            sender_->Remove(pair.first);
        }
    }
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

    QCSettings qcs;
    qcs.AssignedId = ++next_assigned_id_;
    qcs.qs = qs_;
    qcs.dcid = dcid;
    qcs.on_timeout = [this, dcid](uint64_t connection_id) {
        LOG_INFO() << "*** Connection timeout: " << connection_id;
    };
    qcs.on_connect = [this](uint64_t connection_id, const boost::asio::ip::udp::endpoint& peer_endpoint) {
        LOG_INFO() << "*** Connection established: " << connection_id << " " << peer_endpoint.address().to_string();
    };
    QuicheConnection* qcp = qc.get();
    qcs.on_data = [this, qcp](const QuicheMailbox::Event& event) {
        mailbox_.Post(event);
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
