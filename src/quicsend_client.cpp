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
        }

        settings_.OnConnect();
    };
    qcs.on_request = [this](DataStream& stream) {
        settings_.OnRequest(stream);
    };

    connection_ = std::make_shared<QuicheConnection>();
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
    RequestDataType type,
    const void* data,
    int bytes)
{
    if (closed_) {
        return;
    }

    if (!data || bytes == 0) {
        const std::vector<std::pair<std::string, std::string>> headers = {
            {":method", "GET"},
            {":scheme", "https"},
            {":authority", settings_.Host},
            {":path", path},
            {"user-agent", "quiche-quicsend"},
            {"content-length", "0"}
        };

        auto fn = [this](DataStream& stream) {
            mailbox_.Post()
        };

        return connection_->SendRequest(fn, headers);
    }

    const std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "PUT"},
        {":scheme", "https"},
        {":authority", settings_.Host},
        {":path", path},
        {"user-agent", "quiche-quicsend"},
        {"content-type", RequestDataTypeToString(type)},
        {"content-length", std::to_string(bytes)}
    };

    auto fn = [this](DataStream& stream) {
        mailbox_.Post()
    };

    return connection_->SendRequest(fn, headers, data, bytes);
}

void QuicSendClient::Poll(
    OnConnectCallback on_connect,
    OnTimeoutCallback on_timeout,
    OnRequestCallback on_request,
    int poll_msec)
{
    if (closed_) {
        on_timeout();
        return;
    }

    if (!connected_) {
        return;
    }

    if (connected_ && !reported_connection_) {
        on_connect();
        reported_connection_ = true;
    }

}
