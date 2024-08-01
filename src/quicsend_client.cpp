#include <quicsend_client.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Client

QuicSendClient::QuicSendClient(const QuicSendClientSettings& settings)
    : resolver_(io_context_)
{
    settings_ = settings;

    cert_der_ = LoadPEMCertAsDER(settings_.CertPath);

    auto datagram_callback = [this](
        uint8_t* data,
        std::size_t bytes,
        const boost::asio::ip::udp::endpoint& peer_endpoint)
    {
        if (peer_endpoint != resolved_endpoint_) {
            LOG_ERROR() << "received packet from unexpected endpoint";
            return;
        }

        connection_->OnDatagram(data, bytes, peer_endpoint);
    };

    qs_ = std::make_shared<QuicheSocket>(
        io_context_,
        datagram_callback);

    sender_ = std::make_shared<QuicheSender>(qs_);

    connection_ = std::make_shared<QuicheConnection>();

    QCSettings qcs;
    qcs.qs = qs_;
    qcs.dcid = ConnectionId();
    qcs.on_timeout = [this]() {
        Close();
    };
    qcs.on_connect = [this]() {
        LOG_INFO() << "*** Connection established";
        if (cert_der_.empty()) {
            LOG_WARN() << "No peer certificate file provided to check";
        } else if (!connection_->ComparePeerCertificate(cert_der_.data(), cert_der_.size())) {
            LOG_ERROR() << "Peer certificate does not match";
            Close();
            return;
        } else {
            LOG_INFO() << "Verified peer certificate";
            connected_ = true;
        }
    };
    qcs.on_request = [this](DataStream& stream) {
        QuicheMailbox::Event event;
        event.IsResponse = false;
        event.Id = stream.Id;
        event.Type = stream.ContentType;
        event.Connection = connection_;
        event.Buffer = stream.Buffer;

        mailbox_.Post(event);

        stream.Buffer = nullptr;
    };

    connection_->Initialize(qcs);

    resolver_.async_resolve(settings_.Host, std::to_string(settings_.Port),
        [this](const boost::system::error_code& ec, boost::asio::ip::udp::resolver::results_type results) {
            if (!ec) {
                resolved_endpoint_ = *results.begin();
                sender_->Add(connection_->settings_.dcid, connection_);
                if (!connection_->Connect(resolved_endpoint_)) {
                    Close();
                    return;
                }
                qs_->StartReceive();
                connection_->FlushEgress();
            } else {
                LOG_ERROR() << "Failed to resolve host: " << ec.message();
                Close();
                return;
            }
        });

    loop_thread_ = std::make_shared<std::thread>([this]() {
        Loop();
    });
}

QuicSendClient::~QuicSendClient() {
    mailbox_.Shutdown();
    Close();
    JoinThread(loop_thread_);
}

void QuicSendClient::Loop() {
    io_context_.run();
    closed_ = true;
}

void QuicSendClient::Close() {
    if (closed_.exchange(true)) {
        return;
    }

    LOG_INFO() << "*** Connection closed";

    io_context_.stop();
}

int64_t QuicSendClient::Request(
    const std::string& path,
    BodyDataType type,
    const void* data,
    int bytes)
{
    if (closed_) {
        return;
    }

    auto response_callback = [this](DataStream& stream) {
        QuicheMailbox::Event event;
        event.IsResponse = true;
        event.Id = stream.Id;
        event.Type = stream.ContentType;
        event.Connection = connection_;
        event.Buffer = stream.Buffer;

        mailbox_.Post(event);

        stream.Buffer = nullptr;
    };

    if (!data || bytes == 0) {
        const std::vector<std::pair<std::string, std::string>> headers = {
            {":method", "GET"},
            {":scheme", "https"},
            {":authority", settings_.Host},
            {":path", path},
            {"user-agent", "quiche-quicsend"},
            {"content-length", "0"}
        };

        return connection_->SendRequest(response_callback, headers);
    }

    const std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "PUT"},
        {":scheme", "https"},
        {":authority", settings_.Host},
        {":path", path},
        {"user-agent", "quiche-quicsend"},
        {"content-type", BodyDataTypeToString(type)},
        {"content-length", std::to_string(bytes)}
    };

    return connection_->SendRequest(response_callback, headers, data, bytes);
}

void QuicSendClient::Poll(
    OnConnectCallback on_connect,
    OnTimeoutCallback on_timeout,
    OnDataCallback on_data,
    int poll_msec)
{
    if (closed_) {
        if (!reported_timeout_) {
            on_timeout();
            reported_timeout_ = true;
        }
        return;
    }

    if (!connected_) {
        return;
    }

    if (!reported_connect_) {
        on_connect();
        reported_connect_ = true;
    }

    mailbox_.Poll(on_data, poll_msec);
}
