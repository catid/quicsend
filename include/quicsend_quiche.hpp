#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <atomic>
#include <mutex>
#include <random>
#include <cstdint>
#include <functional>
#include <thread>

#include <boost/asio.hpp>
#include <quiche.h>

#include <fcntl.h>


//------------------------------------------------------------------------------
// Constants

#define LOCAL_CONN_ID_LEN 16
#define MAX_DATAGRAM_SEND_SIZE 1350
#define MAX_DATAGRAM_RECV_SIZE 1400 * 2
#define MAX_QUIC_STREAMS 8
#define INITIAL_MAX_DATA 8 * 1024 * 1024
#define INITIAL_MAX_STREAM_DATA 1 * 1024 * 1024
#define QUIC_IDLE_TIMEOUT_MSEC 5000
#define QUIC_SEND_BUFFER_SIZE 8 * 1024 * 1024
#define QUIC_SEND_SLOW_INTERVAL_MSEC 20
#define QUIC_SEND_FAST_INTERVAL_MSEC 10
#define QUIC_CONNECT_TIMEOUT_MSEC 3000
#define QUIC_TLS_CNAME "catid.io"

#define TOKEN_ID static_cast<uint8_t>( 0xdc )
#define MAX_TOKEN_LEN (5 + QUICHE_MAX_CONN_ID_LEN + 16/*IPv6*/)

//#define ENABLE_QUICHE_DEBUG_LOGGING


//------------------------------------------------------------------------------
// Connection Id

struct ConnectionId {
    std::array<uint8_t, LOCAL_CONN_ID_LEN> Id;
    size_t Length = LOCAL_CONN_ID_LEN;

    void Randomize();

    uint8_t* data() { return Id.data(); }
    const uint8_t* data() const { return Id.data(); }

    std::string ToString() const;

    bool operator==(const ConnectionId& other) const;
    bool operator<(const ConnectionId& other) const;
};

struct ConnectionIdHash {
    std::size_t operator()(const ConnectionId& cid) const;
};


//------------------------------------------------------------------------------
// Tools

quiche_config* CreateQuicheConfig(
    const std::string& cert_path = "",
    const std::string& key_path = "");

std::vector<uint8_t> mint_token(
    const ConnectionId& dcid,
    const boost::asio::ip::udp::endpoint& endpoint);

bool read_token(
    const uint8_t *token, size_t token_len,
    const boost::asio::ip::udp::endpoint& endpoint,
    ConnectionId& odcid);

std::pair<sockaddr_storage, socklen_t> to_sockaddr(const boost::asio::ip::udp::endpoint& endpoint);
boost::asio::ip::udp::endpoint sockaddr_to_endpoint(const sockaddr* addr, socklen_t len);

const char* quiche_h3_error_to_string(int error);
const char* quiche_error_to_string(int error);


//------------------------------------------------------------------------------
// SendAllocator

struct SendBuffer {
    uint8_t Payload[MAX_DATAGRAM_SEND_SIZE];
    int Length = 0;
};

class SendAllocator {
public:
    std::shared_ptr<SendBuffer> Allocate();
    void Free(std::shared_ptr<SendBuffer> buffer);

protected:
    std::mutex mutex_;
    std::vector<std::shared_ptr<SendBuffer>> free_buffers_;
    std::atomic<uint32_t> free_buffers_count_ = ATOMIC_VAR_INIT(0);
};


//------------------------------------------------------------------------------
// QuicheSocket

struct QuicheConnection;

using DatagramCallback = std::function<void(
    uint8_t* data,
    std::size_t bytes,
    const boost::asio::ip::udp::endpoint& peer_endpoint)>;

class QuicheSocket {
public:
    friend struct QuicheConnection;

    QuicheSocket(
        boost::asio::io_context& io_context,
        DatagramCallback on_datagram,
        uint16_t port = 0,
        const std::string& cert_path = "",
        const std::string& key_path = "");
    ~QuicheSocket();

    void StartReceive();

    void Send(
        std::shared_ptr<SendBuffer> buffer,
        const boost::asio::ip::udp::endpoint& dest_endpoint);

    // Socket
    SendAllocator allocator_;
    boost::asio::io_context* io_context_ = nullptr;
    std::shared_ptr<boost::asio::ip::udp::socket> socket_;

    quiche_config* config_ = nullptr;

protected:
    // Contexts
    quiche_h3_config* h3_config_ = nullptr;

    // Receive buffer
    std::array<uint8_t, MAX_DATAGRAM_RECV_SIZE> recv_buf_;
    boost::asio::ip::udp::endpoint sender_endpoint_;
    DatagramCallback on_datagram_;

    // Shared between all connections
    std::array<uint8_t, MAX_DATAGRAM_RECV_SIZE> body_buf_;
};


//------------------------------------------------------------------------------
// QuicheMailbox

class QuicheConnection;

enum class BodyDataType {
    Unknown,
    Text,
    Binary
};

const char* BodyDataTypeToString(BodyDataType type);


class QuicheMailbox {
public:
    struct Event {
        bool IsResponse = false; // else it's a response
        int64_t Id = -1;
        BodyDataType Type = BodyDataType::Unknown;
        std::shared_ptr<QuicheConnection> Connection;
        std::shared_ptr<std::vector<uint8_t>> Buffer;
    };

    using MailboxCallback = std::function<void(const Event& event)>;

    void Shutdown();
    // Wait for events.  Pass -1 for timeout_msec to wait indefinitely.
    void Poll(MailboxCallback callback, int timeout_msec = -1);
    void Post(const Event& event);

protected:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> terminated_ = ATOMIC_VAR_INIT(false);

    std::vector<Event> events_;
};


//------------------------------------------------------------------------------
// DataStream

struct DataStream;

using ResponseCallback = std::function<void(DataStream& stream)>;

// Receives data from the network and buffers it
struct DataStream {
    uint64_t Id = MAX_QUIC_STREAMS;

    bool Sending = false;
    int SendOffset = 0;
    std::vector<uint8_t> OutgoingBuffer;

    bool Finished = false;
    std::shared_ptr<std::vector<uint8_t>> Buffer;

    std::string Method, Path, Status;
    BodyDataType ContentType = BodyDataType::Unknown;

    ResponseCallback OnResponse;

    void OnHeader(const std::string& name, const std::string& value);
    void OnData(const void* data, size_t bytes);

    // Returns true on the first finished event for this stream
    bool OnFinished();

    void Reset();
};


//------------------------------------------------------------------------------
// Connection State

using OnTimeoutCallback = std::function<void()>;
using OnConnectCallback = std::function<void()>;
using OnDataCallback = std::function<void(const QuicheMailbox::Event& event)>;

struct QCSettings {
    std::shared_ptr<QuicheSocket> qs;

    ConnectionId dcid;

    OnConnectCallback on_connect;
    OnTimeoutCallback on_timeout;
    OnDataCallback on_request;
};

class QuicheConnection {
public:
    QCSettings settings_;

    ~QuicheConnection();

    void Initialize(const QCSettings& settings);

    bool IsClosed() const {
        return timeout_;
    }

    bool Accept(
        boost::asio::ip::udp::endpoint client_endpoint,
        const ConnectionId& dcid,
        const ConnectionId& odcid);
    bool Connect(boost::asio::ip::udp::endpoint server_endpoint);

    void OnDatagram(
        uint8_t* data,
        std::size_t bytes,
        boost::asio::ip::udp::endpoint peer_endpoint);

    // Returns the stream id or -1 on failure
    int64_t SendRequest(
        ResponseCallback on_response,
        const std::vector<std::pair<std::string, std::string>>& headers,
        const void* data = nullptr,
        int bytes = 0);

    bool SendResponse(
        uint64_t stream_id,
        const std::vector<std::pair<std::string, std::string>>& headers,
        const void* data = nullptr,
        int bytes = 0);

    inline bool FlushEgress() {
        std::shared_ptr<SendBuffer> buffer;
        bool sent = FlushEgress(buffer);
        settings_.qs->allocator_.Free(buffer);
        return sent;
    }
    bool FlushEgress(std::shared_ptr<SendBuffer>& buffer);

    bool ComparePeerCertificate(const void* cert_cer_data, int bytes);

protected:
    std::recursive_mutex mutex_;
    quiche_conn* conn_ = nullptr;
    quiche_h3_conn* http3_ = nullptr;

    std::shared_ptr<boost::asio::deadline_timer> quiche_timer_;
    std::atomic<bool> timeout_ = ATOMIC_VAR_INIT(false);
    std::atomic<bool> timer_set_ = ATOMIC_VAR_INIT(false);

    // If the server does not respond to a connection request, quiche does not flag a timeout
    std::shared_ptr<boost::asio::deadline_timer> connection_timer_;

    boost::asio::ip::udp::endpoint peer_endpoint_;

    DataStream streams_[MAX_QUIC_STREAMS];

    // Called from function with lock held
    bool SendBody(uint64_t stream_id, const void* data, int bytes);
    void ProcessH3Events();
    void TickTimeout();
    void FlushTransfers();
};


//------------------------------------------------------------------------------
// QuicheSender

using QuicheConnectionMap = std::unordered_map<ConnectionId, std::shared_ptr<QuicheConnection>, ConnectionIdHash>;

class QuicheSender {
public:
    QuicheSender(std::shared_ptr<QuicheSocket> qs);
    ~QuicheSender();

    void Add(const ConnectionId& dcid, std::shared_ptr<QuicheConnection> connection);
    std::shared_ptr<QuicheConnection> Find(const ConnectionId& dcid);

protected:
    std::mutex mutex_;

    std::shared_ptr<QuicheSocket> qs_;
    QuicheConnectionMap connections_;

    std::shared_ptr<std::thread> send_thread_;
    std::atomic<bool> terminated_ = ATOMIC_VAR_INIT(false);

    void Loop();
};
