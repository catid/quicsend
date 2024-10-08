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
    auto conn = sender_->Find(connection_id);
    if (conn) {
        conn->Close();
    }
}

void QuicSendServer::Respond(
    uint64_t connection_id,
    int64_t request_id,
    int32_t status,
    const std::string& header_info,
    BodyData body)
{
    if (closed_) {
        return;
    }

    auto conn = sender_->Find(connection_id);
    if (!conn) {
        return;
    }

    if (body.Empty()) {
        const std::vector<std::pair<std::string, std::string>> headers = {
            {":status", std::to_string(status)},
            {"server", QUICSEND_SERVER_AGENT},
            {QUICSEND_HEADER_INFO, header_info},
        };

        conn->SendResponse(request_id, headers);
        return;
    }

    std::vector<std::pair<std::string, std::string>> headers = {
        {":status", std::to_string(status)},
        {"server", QUICSEND_SERVER_AGENT},
        {QUICSEND_HEADER_INFO, header_info},
        {"content-type", body.ContentType},
        {"content-length", std::to_string(body.Length)},
    };

    conn->SendResponse(request_id, headers, body.Data, body.Length);
}

void QuicSendServer::Poll(
    OnDataCallback on_event,
    int timeout_msec)
{
    mailbox_.Poll(on_event, timeout_msec);
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
            SendVersionNegotiation(scid, dcid, peer_endpoint);
            LOG_WARN() << "New connection: Unsupported version " << version << " from " << peer_endpoint;
            return;
        }

        if (token_len == 0) {
            // We require a token to connect to avoid DDoS attacks
            SendRetry(scid, dcid, peer_endpoint);
            return;
        }

        if (!read_token(token, token_len, 
                        peer_endpoint,
                        odcid)) {
            LOG_ERROR() << "Invalid address validation token";
            return;
        }

        conn_ptr = CreateConnection(dcid, odcid, peer_endpoint);
        if (!conn_ptr) {
            LOG_ERROR() << "Failed to create connection";
            return;
        }
    }

    conn_ptr->OnDatagram(data, bytes, peer_endpoint);
}

void QuicSendServer::SendVersionNegotiation(
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

void QuicSendServer::SendRetry(
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

std::shared_ptr<QuicheConnection> QuicSendServer::CreateConnection(
    const ConnectionId& dcid,
    const ConnectionId& odcid,
    const boost::asio::ip::udp::endpoint& peer_endpoint)
{
    std::shared_ptr<QuicheConnection> qc = std::make_shared<QuicheConnection>();

    QCSettings qcs;
    qcs.IsServer = true;
    qcs.AssignedId = ++next_assigned_id_;
    qcs.qs = qs_;
    qcs.dcid = dcid;
    qcs.on_timeout = [this, dcid](uint64_t connection_id) {
        LOG_INFO() << "*** Link timeout: " << connection_id;

        // Queue timeout event
        QuicheMailbox::Event event;
        event.Type = QuicheMailbox::EventType::Timeout;
        event.ConnectionAssignedId = connection_id;
        mailbox_.Post(event);
    };
    qcs.on_connect = [this](uint64_t connection_id, const boost::asio::ip::udp::endpoint& peer_endpoint) {
        LOG_INFO() << "*** Link established: " << connection_id << " " << EndpointToString(peer_endpoint);
    };
    QuicheConnection* qc_weak = qc.get();
    qcs.on_data = [this, qc_weak](const QuicheMailbox::Event& event) {
        if (!qc_weak->IsConnected()) {
            if (event.Stream->Authorization != settings_.Authorization) {
                LOG_WARN() << "*** Link closed: Invalid auth token";
                qc_weak->Close("invalid auth token");
                return;
            }

            qc_weak->MarkClientConnected();

            // Queue connect event
            QuicheMailbox::Event event;
            event.Type = QuicheMailbox::EventType::Connect;
            event.ConnectionAssignedId = event.ConnectionAssignedId;
            event.PeerEndpoint = event.PeerEndpoint;
            mailbox_.Post(event);
        }

        mailbox_.Post(event);
    };

    qc->Initialize(qcs);
    if (!qc->Accept(peer_endpoint, dcid, odcid)) {
        return nullptr;
    }

    sender_->Add(qc);
    return qc;
}
