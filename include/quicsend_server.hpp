#pragma once

#include <quicsend_quiche.hpp>
#include <quicsend_tools.hpp>


//------------------------------------------------------------------------------
// HTTP/3 Server

class QuicSendServer {
public:
    QuicSendServer(
        uint16_t port,
        const std::string& cert_path,
        const std::string& key_path);
    ~QuicSendServer();

    bool IsRunning() const {
        return !closed_;
    }

    void Close();

private:
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

    void Loop();
};
