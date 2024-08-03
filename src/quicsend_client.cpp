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
    qcs.on_timeout = [this](int32_t /*connection_id*/) {
        Close();
    };
    qcs.on_connect = [this](int32_t /*connection_id*/, const boost::asio::ip::udp::endpoint& /*peer_endpoint*/) {
        if (connection_->ComparePeerCertificate(cert_der_.data(), cert_der_.size())) {
            LOG_INFO() << "*** Connection established";
        }
    };
    qcs.on_data = [this](const QuicheMailbox::Event& event) {
        mailbox_.Post(event);
    };

    connection_->Initialize(qcs);

    resolver_.async_resolve(settings_.Host, std::to_string(settings_.Port),
        [this](const boost::system::error_code& ec, boost::asio::ip::udp::resolver::results_type results) {
            if (!ec) {
                resolved_endpoint_ = *results.begin();
                sender_->Add(connection_);
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
        io_context_.run();
        closed_ = true;
    });
}

QuicSendClient::~QuicSendClient() {
    mailbox_.Shutdown();
    Close();
    JoinThread(loop_thread_);
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
        return -1;
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

        return connection_->SendRequest(headers);
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

    return connection_->SendRequest(headers, data, bytes);
}

void QuicSendClient::Poll(
    OnConnectCallback on_connect,
    OnTimeoutCallback on_timeout,
    OnDataCallback on_data,
    int timeout_msec)
{
    mailbox_.Poll(on_data, timeout_msec);

    connection_->Poll(on_connect, on_timeout);
}
