#pragma once

#include <quicsend_quiche.hpp>
#include <quicsend_tools.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Client

struct QuicSendClientSettings {
    std::string Host;
    uint16_t Port;
    std::string CertPath;
};

class QuicSendClient {
public:
    QuicSendClient(const QuicSendClientSettings& settings);
    ~QuicSendClient();

    bool IsRunning() const {
        return !closed_;
    }

    void Close();

    int64_t Request(
        const std::string& path,
        RequestDataType type,
        const void* data,
        int bytes);

    void Poll(
        OnConnectCallback on_connect,
        OnTimeoutCallback on_timeout,
        OnRequestCallback on_request,
        int poll_msec = 100);

private:
    QuicSendClientSettings settings_;

    boost::asio::io_context io_context_;
    std::vector<uint8_t> cert_der_;

    boost::asio::ip::udp::resolver resolver_;
    boost::asio::ip::udp::endpoint resolved_endpoint_;

    std::shared_ptr<QuicheSocket> qs_;
    std::shared_ptr<QuicheConnection> connection_;
    std::shared_ptr<QuicheSender> sender_;

    std::shared_ptr<std::thread> loop_thread_;
    std::atomic<bool> closed_ = ATOMIC_VAR_INIT(false);

    std::atomic<bool> connected_ = ATOMIC_VAR_INIT(false);
    std::atomic<bool> reported_connection_ = ATOMIC_VAR_INIT(false);

    QuicSendMailbox mailbox_;

    void Loop();
};
