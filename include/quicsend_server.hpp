#pragma once

#include <quicsend_quiche.hpp>
#include <quicsend_tools.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Server

struct QuicSendServerSettings {
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
        BodyDataType type,
        const void* data,
        int bytes);

    void Poll(
        OnConnectCallback on_connect,
        OnTimeoutCallback on_timeout,
        OnDataCallback on_request,
        int poll_msec = 100);

protected:
    QuicSendServerSettings settings_;

    void OnDatagram(
        uint8_t* data,
        std::size_t bytes,
        const boost::asio::ip::udp::endpoint& peer_endpoint);

    void send_version_negotiation(
        const ConnectionId& scid,
        const ConnectionId& dcid,
        const boost::asio::ip::udp::endpoint& peer_endpoint);
    void send_retry(
        const ConnectionId& scid,
        const ConnectionId& dcid,
        const boost::asio::ip::udp::endpoint& peer_endpoint);

    std::shared_ptr<QuicheConnection> create_conn(
        const ConnectionId& dcid,
        const ConnectionId& odcid,
        const boost::asio::ip::udp::endpoint& peer_endpoint);
    void OnRequest(QuicheConnection* qcp, DataStream& stream);

    boost::asio::io_context io_context_;

    std::shared_ptr<QuicheSocket> qs_;
    std::shared_ptr<QuicheSender> sender_;
    QuicheMailbox mailbox_;

    std::shared_ptr<std::thread> loop_thread_;
    std::atomic<bool> closed_ = ATOMIC_VAR_INIT(false);

    uint64_t next_assigned_id_ = 0;

    void Loop();
};
