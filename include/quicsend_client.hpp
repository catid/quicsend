#pragma once

#include <quicsend_quiche.hpp>
#include <quicsend_tools.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Client

struct QuicSendClientSettings {
    std::string Authorization;
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
        BodyData body);

    void Poll(
        OnConnectCallback on_connect,
        OnTimeoutCallback on_timeout,
        OnDataCallback on_request,
        int timeout_msec = 100);

    void Respond(
        int64_t request_id,
        int32_t status,
        BodyData body);

private:
    QuicSendClientSettings settings_;

    boost::asio::io_context io_context_;
    std::vector<uint8_t> cert_der_;

    boost::asio::ip::udp::resolver resolver_;
    boost::asio::ip::udp::endpoint resolved_endpoint_;

    std::shared_ptr<QuicheSocket> qs_;
    std::shared_ptr<QuicheConnection> connection_;
    std::shared_ptr<QuicheSender> sender_;
    QuicheMailbox mailbox_;

    std::shared_ptr<std::thread> loop_thread_;
    std::atomic<bool> closed_ = ATOMIC_VAR_INIT(false);
};
