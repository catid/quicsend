#pragma once

#include <quicsend_quiche.hpp>
#include <quicsend_tools.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Server

struct QuicSendServerSettings {
    std::string AuthToken;
    uint16_t Port;
    std::string KeyPath;
    std::string CertPath;
};

class QuicSendServer {
public:
    QuicSendServer(const QuicSendServerSettings& settings);
    ~QuicSendServer();

    bool IsRunning() const {
        return !closed_;
    }

    // API calls
    void Close(uint64_t connection_id);

    int64_t Request(
        uint64_t connection_id,
        const std::string& path,
        BodyData body);

    void Respond(
        uint64_t connection_id,
        int64_t request_id,
        int32_t status,
        BodyData body);

    // Returns false if the server is closed
    bool Poll(
        OnConnectCallback on_connect,
        OnTimeoutCallback on_timeout,
        OnDataCallback on_data,
        int timeout_msec = 100);

protected:
    QuicSendServerSettings settings_;

    void OnDatagram(
        uint8_t* data,
        std::size_t bytes,
        const boost::asio::ip::udp::endpoint& peer_endpoint);

    void SendVersionNegotiation(
        const ConnectionId& scid,
        const ConnectionId& dcid,
        const boost::asio::ip::udp::endpoint& peer_endpoint);
    void SendRetry(
        const ConnectionId& scid,
        const ConnectionId& dcid,
        const boost::asio::ip::udp::endpoint& peer_endpoint);

    std::shared_ptr<QuicheConnection> CreateConnection(
        const ConnectionId& dcid,
        const ConnectionId& odcid,
        const boost::asio::ip::udp::endpoint& peer_endpoint);

    boost::asio::io_context io_context_;

    std::shared_ptr<QuicheSocket> qs_;
    std::shared_ptr<QuicheSender> sender_;
    QuicheMailbox mailbox_;

    std::shared_ptr<std::thread> loop_thread_;
    std::atomic<bool> closed_ = ATOMIC_VAR_INIT(false);

    uint64_t next_assigned_id_ = 0;
};
