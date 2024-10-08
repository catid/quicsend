#include "quicsend_quiche.hpp"
#include "quicsend_tools.hpp"

#include <iomanip>


//------------------------------------------------------------------------------
// Quiche Connection Id

bool ConnectionId::operator==(const ConnectionId& other) const {
    if (Length != other.Length) {
        return false;
    }
    return std::memcmp(data(), other.data(), Length) == 0;
}

bool ConnectionId::operator<(const ConnectionId& other) const {
    return std::memcmp(data(), other.data(), Length) < 0;
}

void ConnectionId::Randomize() {
    std::random_device rd;  // Uses /dev/urandom on Linux
    std::generate(Id.begin(), Id.end(), [&rd]() { return static_cast<uint8_t>(rd()); });
    Length = LOCAL_CONN_ID_LEN;
}

std::string ConnectionId::ToString() const
{
    std::stringstream ss;
    ss << std::hex;
    for (size_t i = 0; i < Length; i++) {
        ss << std::setfill('0') << std::setw(2) << static_cast<int>(Id[i]);
    }
    ss << std::dec << " (" << Length << " bytes)";
    return ss.str();
}

std::size_t ConnectionIdHash::operator()(const ConnectionId& cid) const {
    // FNV-1a hash: http://isthe.com/chongo/tech/comp/fnv/
    uint32_t hash = 0x811C9DC5;
    for (size_t i = 0; i < cid.Length; i++) {
        hash = (cid.Id[i] ^ hash) * 0x01000193;
    }
    return hash;
}


//------------------------------------------------------------------------------
// Tools

quiche_config* CreateQuicheConfig(
    const std::string& cert_path,
    const std::string& key_path)
{
    quiche_config* config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (!config) {
        return nullptr;
    }

    quiche_config_load_cert_chain_from_pem_file(config, cert_path.c_str());
    quiche_config_load_priv_key_from_pem_file(config, key_path.c_str());
    quiche_config_set_application_protos(config,
        (uint8_t *) QUICHE_H3_APPLICATION_PROTOCOL,
        sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1);

    quiche_config_set_max_idle_timeout(config, QUIC_IDLE_TIMEOUT_MSEC);

    quiche_config_set_max_recv_udp_payload_size(config, MAX_DATAGRAM_SEND_SIZE);
    quiche_config_set_max_send_udp_payload_size(config, MAX_DATAGRAM_SEND_SIZE);

    quiche_config_set_initial_max_data(config, INITIAL_MAX_DATA);
    quiche_config_set_initial_max_stream_data_bidi_local(config, INITIAL_MAX_STREAM_DATA);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, INITIAL_MAX_STREAM_DATA);
    quiche_config_set_initial_max_stream_data_uni(config, INITIAL_MAX_STREAM_DATA);

    quiche_config_set_initial_max_streams_bidi(config, MAX_PARALLEL_QUIC_STREAMS);
    quiche_config_set_initial_max_streams_uni(config, MAX_PARALLEL_QUIC_STREAMS);

    // Disable active migration to avoid unnecessary delays.
    // This feature is only useful for mobile clients.
    quiche_config_set_disable_active_migration(config, true);

    // Allow 0-RTT
    quiche_config_enable_early_data(config);

    // Configure packet pacing (default is true)
    quiche_config_enable_pacing(config, true);

    // Latest congestion control algorithm
    // Note: QUICHE_CC_BBR2 seems to be a bit slower than QUICHE_CC_BBR
    quiche_config_set_cc_algorithm(config, QUICHE_CC_BBR);

    // Enable peer certificate verification
    quiche_config_verify_peer(config, true);

    return config;
}


//------------------------------------------------------------------------------
// Token Serialization

std::vector<uint8_t> mint_token(
    const ConnectionId& dcid,
    const boost::asio::ip::udp::endpoint& endpoint)
{
    int dcid_len = dcid.Length;
    int len = 5 + dcid_len;
    len += endpoint.address().is_v4() ? 4 : 16;
    std::vector<uint8_t> token(len);

    token[0] = TOKEN_ID;
    token[1] = static_cast<uint8_t>( dcid_len );
    token[2] = static_cast<uint8_t>( endpoint.address().is_v4() );

    write_uint16_le(token.data() + 3, endpoint.port());

    std::memcpy(token.data() + 5, dcid.data(), dcid_len);

    if (endpoint.address().is_v4()) {
        uint32_t addr = endpoint.address().to_v4().to_ulong();
        write_uint32_le(token.data() + dcid_len + 5, addr);
    } else {
        auto v6addr = endpoint.address().to_v6().to_bytes();
        std::memcpy(token.data() + dcid_len + 5, v6addr.data(), v6addr.size());
    }

    return token;
}

bool read_token(
    const uint8_t *token, size_t token_len,
    const boost::asio::ip::udp::endpoint& endpoint,
    ConnectionId& odcid)
{
    if (token_len < 5 + 4) {
        return false;
    }

    if (token[0] != TOKEN_ID) {
        return false;
    }
    size_t dcid_len = token[1];
    bool is_v4 = token[2] != 0;
    uint16_t port = read_uint16_le(token + 3);
    token += 5;
    token_len -= 5;
    if (endpoint.port() != port) {
        return false;
    }

    if (token_len < dcid_len) {
        return false;
    }
    std::memcpy(odcid.data(), token, dcid_len);
    odcid.Length = dcid_len;
    token += dcid_len;
    token_len -= dcid_len;

    if (is_v4) {
        if (token_len < 4) {
            return false;
        }

        uint32_t addr = read_uint32_le(token);
        if (endpoint.address().to_v4().to_ulong() != addr) {
            return false;
        }
    } else {
        if (token_len < 16) {
            return false;
        }

        auto v6addr = endpoint.address().to_v6().to_bytes();
        if (std::memcmp(token, v6addr.data(), 16) != 0) {
            return false;
        }
    }

    return true;
}


//------------------------------------------------------------------------------
// Socket address conversion

std::pair<sockaddr_storage, socklen_t> to_sockaddr(const boost::asio::ip::udp::endpoint& endpoint) {
    sockaddr_storage storage;
    socklen_t size;

    if (endpoint.protocol() == boost::asio::ip::udp::v4()) {
        auto& addr = reinterpret_cast<sockaddr_in&>(storage);
        addr.sin_family = AF_INET;
        addr.sin_port = htons(endpoint.port());
        addr.sin_addr.s_addr = endpoint.address().to_v4().to_ulong();
        size = sizeof(sockaddr_in);
    } else {
        auto& addr = reinterpret_cast<sockaddr_in6&>(storage);
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(endpoint.port());
        auto bytes = endpoint.address().to_v6().to_bytes();
        std::memcpy(&addr.sin6_addr, bytes.data(), bytes.size());
        size = sizeof(sockaddr_in6);
    }

    return {storage, size};
}

boost::asio::ip::udp::endpoint sockaddr_to_endpoint(const sockaddr* addr, socklen_t len) {
    if (addr->sa_family == AF_INET) {
        // IPv4
        const sockaddr_in* addr_in = reinterpret_cast<const sockaddr_in*>(addr);
        if (len < sizeof(sockaddr_in)) {
            throw std::runtime_error("Invalid length for IPv4 address");
        }
        return boost::asio::ip::udp::endpoint(
            boost::asio::ip::address_v4(addr_in->sin_addr.s_addr),
            ntohs(addr_in->sin_port)
        );
    } else if (addr->sa_family == AF_INET6) {
        // IPv6
        const sockaddr_in6* addr_in6 = reinterpret_cast<const sockaddr_in6*>(addr);
        if (len < sizeof(sockaddr_in6)) {
            throw std::runtime_error("Invalid length for IPv6 address");
        }
        boost::asio::ip::address_v6::bytes_type bytes;
        std::memcpy(bytes.data(), addr_in6->sin6_addr.s6_addr, bytes.size());
        return boost::asio::ip::udp::endpoint(
            boost::asio::ip::address_v6(bytes),
            ntohs(addr_in6->sin6_port)
        );
    } else {
        throw std::runtime_error("Unsupported address family");
    }
}


//------------------------------------------------------------------------------
// Error Strings

const char* quiche_h3_error_to_string(int error) {
    switch (error) {
    case QUICHE_H3_ERR_DONE:
        return "QUICHE_H3_ERR_DONE";
    case QUICHE_H3_ERR_BUFFER_TOO_SHORT:
        return "QUICHE_H3_ERR_BUFFER_TOO_SHORT";
    case QUICHE_H3_ERR_INTERNAL_ERROR:
        return "QUICHE_H3_ERR_INTERNAL_ERROR";
    case QUICHE_H3_ERR_EXCESSIVE_LOAD:
        return "QUICHE_H3_ERR_EXCESSIVE_LOAD";
    case QUICHE_H3_ERR_ID_ERROR:
        return "QUICHE_H3_ERR_ID_ERROR";
    case QUICHE_H3_ERR_STREAM_CREATION_ERROR:
        return "QUICHE_H3_ERR_STREAM_CREATION_ERROR";
    case QUICHE_H3_ERR_CLOSED_CRITICAL_STREAM:
        return "QUICHE_H3_ERR_CLOSED_CRITICAL_STREAM";
    case QUICHE_H3_ERR_MISSING_SETTINGS:
        return "QUICHE_H3_ERR_MISSING_SETTINGS";
    case QUICHE_H3_ERR_FRAME_UNEXPECTED:
        return "QUICHE_H3_ERR_FRAME_UNEXPECTED";
    case QUICHE_H3_ERR_FRAME_ERROR:
        return "QUICHE_H3_ERR_FRAME_ERROR";
    case QUICHE_H3_ERR_QPACK_DECOMPRESSION_FAILED:
        return "QUICHE_H3_ERR_QPACK_DECOMPRESSION_FAILED";
    case QUICHE_H3_ERR_STREAM_BLOCKED:
        return "QUICHE_H3_ERR_STREAM_BLOCKED";
    case QUICHE_H3_ERR_SETTINGS_ERROR:
        return "QUICHE_H3_ERR_SETTINGS_ERROR";
    case QUICHE_H3_ERR_REQUEST_REJECTED:
        return "QUICHE_H3_ERR_REQUEST_REJECTED";
    case QUICHE_H3_ERR_REQUEST_CANCELLED:
        return "QUICHE_H3_ERR_REQUEST_CANCELLED";
    case QUICHE_H3_ERR_REQUEST_INCOMPLETE:
        return "QUICHE_H3_ERR_REQUEST_INCOMPLETE";
    case QUICHE_H3_ERR_MESSAGE_ERROR:
        return "QUICHE_H3_ERR_MESSAGE_ERROR";
    case QUICHE_H3_ERR_CONNECT_ERROR:
        return "QUICHE_H3_ERR_CONNECT_ERROR";
    case QUICHE_H3_ERR_VERSION_FALLBACK:
        return "QUICHE_H3_ERR_VERSION_FALLBACK";
    case QUICHE_H3_TRANSPORT_ERR_DONE:
        return "QUICHE_H3_TRANSPORT_ERR_DONE";
    case QUICHE_H3_TRANSPORT_ERR_BUFFER_TOO_SHORT:
        return "QUICHE_H3_TRANSPORT_ERR_BUFFER_TOO_SHORT";
    case QUICHE_H3_TRANSPORT_ERR_UNKNOWN_VERSION:
        return "QUICHE_H3_TRANSPORT_ERR_UNKNOWN_VERSION";
    case QUICHE_H3_TRANSPORT_ERR_INVALID_FRAME:
        return "QUICHE_H3_TRANSPORT_ERR_INVALID_FRAME";
    case QUICHE_H3_TRANSPORT_ERR_INVALID_PACKET:
        return "QUICHE_H3_TRANSPORT_ERR_INVALID_PACKET";
    case QUICHE_H3_TRANSPORT_ERR_INVALID_STATE:
        return "QUICHE_H3_TRANSPORT_ERR_INVALID_STATE";
    case QUICHE_H3_TRANSPORT_ERR_INVALID_STREAM_STATE:
        return "QUICHE_H3_TRANSPORT_ERR_INVALID_STREAM_STATE";
    case QUICHE_H3_TRANSPORT_ERR_INVALID_TRANSPORT_PARAM:
        return "QUICHE_H3_TRANSPORT_ERR_INVALID_TRANSPORT_PARAM";
    case QUICHE_H3_TRANSPORT_ERR_CRYPTO_FAIL:
        return "QUICHE_H3_TRANSPORT_ERR_CRYPTO_FAIL";
    case QUICHE_H3_TRANSPORT_ERR_TLS_FAIL:
        return "QUICHE_H3_TRANSPORT_ERR_TLS_FAIL";
    case QUICHE_H3_TRANSPORT_ERR_FLOW_CONTROL:
        return "QUICHE_H3_TRANSPORT_ERR_FLOW_CONTROL";
    case QUICHE_H3_TRANSPORT_ERR_STREAM_LIMIT:
        return "QUICHE_H3_TRANSPORT_ERR_STREAM_LIMIT";
    case QUICHE_H3_TRANSPORT_ERR_STREAM_STOPPED:
        return "QUICHE_H3_TRANSPORT_ERR_STREAM_STOPPED";
    case QUICHE_H3_TRANSPORT_ERR_STREAM_RESET:
        return "QUICHE_H3_TRANSPORT_ERR_STREAM_RESET";
    case QUICHE_H3_TRANSPORT_ERR_FINAL_SIZE:
        return "QUICHE_H3_TRANSPORT_ERR_FINAL_SIZE";
    case QUICHE_H3_TRANSPORT_ERR_CONGESTION_CONTROL:
        return "QUICHE_H3_TRANSPORT_ERR_CONGESTION_CONTROL";
    case QUICHE_H3_TRANSPORT_ERR_ID_LIMIT:
        return "QUICHE_H3_TRANSPORT_ERR_ID_LIMIT";
    case QUICHE_H3_TRANSPORT_ERR_OUT_OF_IDENTIFIERS:
        return "QUICHE_H3_TRANSPORT_ERR_OUT_OF_IDENTIFIERS";
    case QUICHE_H3_TRANSPORT_ERR_KEY_UPDATE:
        return "QUICHE_H3_TRANSPORT_ERR_KEY_UPDATE";
    default:
        break;
    }
    return "UNKNOWN_ERROR";
}

const char* quiche_error_to_string(int error) {
    switch (error) {
    case QUICHE_ERR_DONE:
        return "QUICHE_ERR_DONE";
    case QUICHE_ERR_BUFFER_TOO_SHORT:
        return "QUICHE_ERR_BUFFER_TOO_SHORT";
    case QUICHE_ERR_UNKNOWN_VERSION:
        return "QUICHE_ERR_UNKNOWN_VERSION";
    case QUICHE_ERR_INVALID_FRAME:
        return "QUICHE_ERR_INVALID_FRAME";
    case QUICHE_ERR_INVALID_PACKET:
        return "QUICHE_ERR_INVALID_PACKET";
    case QUICHE_ERR_INVALID_STATE:
        return "QUICHE_ERR_INVALID_STATE";
    case QUICHE_ERR_INVALID_STREAM_STATE:
        return "QUICHE_ERR_INVALID_STREAM_STATE";
    case QUICHE_ERR_INVALID_TRANSPORT_PARAM:
        return "QUICHE_ERR_INVALID_TRANSPORT_PARAM";
    case QUICHE_ERR_CRYPTO_FAIL:
        return "QUICHE_ERR_CRYPTO_FAIL";
    case QUICHE_ERR_TLS_FAIL:
        return "QUICHE_ERR_TLS_FAIL";
    case QUICHE_ERR_FLOW_CONTROL:
        return "QUICHE_ERR_FLOW_CONTROL";
    case QUICHE_ERR_STREAM_LIMIT:
        return "QUICHE_ERR_STREAM_LIMIT";
    case QUICHE_ERR_STREAM_STOPPED:
        return "QUICHE_ERR_STREAM_STOPPED";
    case QUICHE_ERR_STREAM_RESET:
        return "QUICHE_ERR_STREAM_RESET";
    case QUICHE_ERR_FINAL_SIZE:
        return "QUICHE_ERR_FINAL_SIZE";
    case QUICHE_ERR_CONGESTION_CONTROL:
        return "QUICHE_ERR_CONGESTION_CONTROL";
    case QUICHE_ERR_ID_LIMIT:
        return "QUICHE_ERR_ID_LIMIT";
    case QUICHE_ERR_OUT_OF_IDENTIFIERS:
        return "QUICHE_ERR_OUT_OF_IDENTIFIERS";
    case QUICHE_ERR_KEY_UPDATE:
        return "QUICHE_ERR_KEY_UPDATE";
    case QUICHE_ERR_CRYPTO_BUFFER_EXCEEDED:
        return "QUICHE_ERR_CRYPTO_BUFFER_EXCEEDED";
    default:
        break;
    }
    return "UNKNOWN_ERROR";
}


//------------------------------------------------------------------------------
// SendAllocator

std::shared_ptr<SendBuffer> SendAllocator::Allocate()
{
    std::shared_ptr<SendBuffer> buffer;

    if (free_buffers_count_ > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!free_buffers_.empty()) {
            buffer = free_buffers_.back();
            free_buffers_.pop_back();
            free_buffers_count_--;
        }
    }

    if (!buffer) {
        buffer = std::make_shared<SendBuffer>();
    }

    return buffer;
}

void SendAllocator::Free(std::shared_ptr<SendBuffer> buffer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    free_buffers_.push_back(buffer);
    free_buffers_count_++;
}


//------------------------------------------------------------------------------
// QuicheSocket

QuicheSocket::QuicheSocket(
    boost::asio::io_context& io_context,
    DatagramCallback on_datagram,
    uint16_t port,
    const std::string& cert_path,
    const std::string& key_path)
{
    io_context_ = &io_context;
    on_datagram_ = on_datagram;

    socket_ = std::make_shared<boost::asio::ip::udp::socket>(
        io_context,
        boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port));

    socket_->set_option(boost::asio::socket_base::receive_buffer_size(QUIC_SEND_BUFFER_SIZE));
    socket_->set_option(boost::asio::socket_base::send_buffer_size(QUIC_SEND_BUFFER_SIZE));
    socket_->set_option(boost::asio::socket_base::reuse_address(true));

    config_ = CreateQuicheConfig(cert_path, key_path);

    if (std::getenv("SSLKEYLOGFILE")) {
        quiche_config_log_keys(config_);
    }

    h3_config_ = quiche_h3_config_new();
    if (!h3_config_) {
        throw std::runtime_error("Failed to create HTTP/3 config");
    }
}

QuicheSocket::~QuicheSocket() {
    if (h3_config_) {
        quiche_h3_config_free(h3_config_);
    }
    if (config_) {
        quiche_config_free(config_);
    }
}

void QuicheSocket::StartReceive() {
    auto fn = [this](boost::system::error_code ec, std::size_t bytes) {
        if (!ec && bytes > 0) {
            on_datagram_(recv_buf_.data(), bytes, sender_endpoint_);
        }
        StartReceive();
    };

    socket_->async_receive_from(
        boost::asio::buffer(recv_buf_),
        sender_endpoint_,
        fn);
}

void QuicheSocket::Send(
    std::shared_ptr<SendBuffer> buffer,
    const boost::asio::ip::udp::endpoint& dest_endpoint)
{
    // FIXME: Use sendmsg() for packet pacing here

    auto fn = [this, buffer](
        const boost::system::error_code& error,
        std::size_t bytes_transferred)
    {
        if (error) {
            LOG_WARN() << "async_send_to failed: " << error.message();
        } else if (bytes_transferred != static_cast<size_t>(buffer->Length)) {
            LOG_WARN() << "async_send_to failed: only " << bytes_transferred << " of " << buffer->Length << " bytes sent";
        }
        allocator_.Free(buffer);
    };

    socket_->async_send_to(
        boost::asio::buffer(buffer->Payload, buffer->Length),
        dest_endpoint,
        fn);
}


//------------------------------------------------------------------------------
// IncomingStream

void IncomingStream::OnHeader(const std::string& name, const std::string& value)
{
    if (name == ":method") {
        Method = value;
    } else if (name == ":path") {
        Path = value;
    } else if (name == ":status") {
        Status = value;
    } else if (name == "Authorization") {
        Authorization = value;
    } else if (name == "content-type") {
        ContentType = value;
    } else if (name == QUICSEND_HEADER_INFO) {
        HeaderInfo = value;
    }
}

void IncomingStream::OnData(const void* data, size_t bytes)
{
    const uint8_t* data8 = reinterpret_cast<const uint8_t*>(data);
    Buffer.insert(Buffer.end(), data8, data8 + bytes);
}


//------------------------------------------------------------------------------
// Quiche Connection

void QuicheConnection::Initialize(const QCSettings& settings)
{
    settings_ = settings;

    connection_timer_ = std::make_shared<boost::asio::deadline_timer>(
        *settings.qs->io_context_);
    quiche_timer_ = std::make_shared<boost::asio::deadline_timer>(
        *settings.qs->io_context_);
}

QuicheConnection::~QuicheConnection() {
    if (conn_) {
        quiche_conn_free(conn_);
    }
    if (http3_) {
        quiche_h3_conn_free(http3_);
    }
}

bool QuicheConnection::Accept(
    boost::asio::ip::udp::endpoint client_endpoint,
    const ConnectionId& dcid,
    const ConnectionId& odcid)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto [local_addr, local_size] = to_sockaddr(settings_.qs->socket_->local_endpoint());
    auto [peer_addr, peer_size] = to_sockaddr(client_endpoint);

    conn_ = quiche_accept(
        dcid.data(), dcid.Length,
        odcid.data(), odcid.Length,
        reinterpret_cast<struct sockaddr *>(&local_addr), local_size,
        reinterpret_cast<struct sockaddr *>(&peer_addr), peer_size,
        settings_.qs->config_); 
    if (!conn_) {
        LOG_ERROR() << "quiche_accept: Failed to create connection";
        return false;
    }
    return true;
}

bool QuicheConnection::Connect(boost::asio::ip::udp::endpoint server_endpoint)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    peer_endpoint_ = server_endpoint;

    ConnectionId scid;
    scid.Randomize();

    auto [local_addr, local_size] = to_sockaddr(settings_.qs->socket_->local_endpoint());
    auto [peer_addr, peer_size] = to_sockaddr(server_endpoint);

    sockaddr* local_sa = reinterpret_cast<sockaddr*>(&local_addr);
    sockaddr* peer_sa = reinterpret_cast<sockaddr*>(&peer_addr);

    conn_ = quiche_connect(
        QUIC_TLS_CNAME,
        scid.data(), scid.Length,
        local_sa, local_size,
        peer_sa, peer_size,
        settings_.qs->config_);
    if (!conn_) {
        LOG_ERROR() << "quiche_connect: Failed to create connection";
        return false;
    }

    connection_timer_->expires_from_now(boost::posix_time::milliseconds(QUIC_CONNECT_TIMEOUT_MSEC));
    connection_timer_->async_wait([this, server_endpoint](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(mutex_);

        if (quiche_conn_is_established(conn_)) {
            return;
        }

        LOG_INFO() << "Connection timed out: Retrying";

        Connect(server_endpoint);
    });
    return true;
}

void QuicheConnection::OnDatagram(
    uint8_t* data,
    std::size_t bytes,
    boost::asio::ip::udp::endpoint peer_endpoint)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    peer_endpoint_ = peer_endpoint;

    auto [peer_addr, peer_size] = to_sockaddr(peer_endpoint);
    auto [local_addr, local_size] = to_sockaddr(settings_.qs->socket_->local_endpoint());

    quiche_recv_info recv_info = {
        reinterpret_cast<struct sockaddr *>(&peer_addr), peer_size,
        reinterpret_cast<struct sockaddr *>(&local_addr), local_size,
    };

    ssize_t done = quiche_conn_recv(
        conn_,
        data,
        bytes,
        &recv_info);
    if (done < 0) {
        LOG_ERROR() << "quiche_conn_recv failed to process packet: " << done << " " << quiche_error_to_string(done);
        return;
    }

    if (quiche_conn_is_established(conn_)) {
        if (!http3_) {
            http3_ = quiche_h3_conn_new_with_transport(conn_, settings_.qs->h3_config_);
            if (!http3_) {
                LOG_ERROR() << "failed to create HTTP/3 connection";
                return;
            }

            settings_.on_connect(settings_.AssignedId, peer_endpoint_);
        }

        ProcessH3Events();
    }

    if (!timeout_) {
        if (quiche_conn_is_closed(conn_)) {
            settings_.on_timeout(settings_.AssignedId);
            timeout_ = true;
            return;
        }
    }

    FlushEgress();
}

void QuicheConnection::Close(const char* reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!timeout_) {
        if (http3_) {
            int r = quiche_h3_send_goaway(http3_, conn_, highest_processed_stream_id_);
            if (r >= 0) {
                return;
            }
            goaway_sent_ = true;
        }

        quiche_conn_close(conn_, true, 0, (const uint8_t*)reason, strlen(reason));
    }
}

void QuicheConnection::TickTimeout() {
    // Called from function with lock held

    if (quiche_conn_is_closed(conn_)) {
        if (!timeout_) {
            settings_.on_timeout(settings_.AssignedId);
        }
        timeout_ = true;
        return;
    }

    // Already set the timer
    if (timer_set_) {
        return;
    }

    uint64_t timeout_msec = quiche_conn_timeout_as_millis(conn_);
    if (timeout_msec == 0) {
        quiche_conn_on_timeout(conn_);
        return;
    }

    auto timeout = boost::posix_time::milliseconds(timeout_msec);
    quiche_timer_->expires_from_now(timeout);
    timer_set_ = true;
    quiche_timer_->async_wait([this](const boost::system::error_code& ec) {
        timer_set_ = false;

        // Cases where we ignore the timer
        if (ec || timeout_) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(mutex_);

        quiche_conn_on_timeout(conn_);
        FlushEgress(); // Flush egress to ensure that disconnection message is sent
    });
}

bool QuicheConnection::FlushEgress(std::shared_ptr<SendBuffer>& buffer) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    FlushCachedResponses();
    FlushTransfers();

    bool sent = false;

    for (;;) {
        if (!buffer) {
            buffer = settings_.qs->allocator_.Allocate();
        }

        quiche_send_info send_info = {};
        ssize_t written = quiche_conn_send(
            conn_,
            buffer->Payload,
            sizeof(buffer->Payload),
            &send_info);
        if (written == QUICHE_ERR_DONE) {
            break;
        }
        if (written < 0) {
            LOG_ERROR() << "failed to create packet: " << written << " " << quiche_error_to_string(written);
            return sent;
        }
        buffer->Length = written;

        auto dest_endpoint = sockaddr_to_endpoint(
            reinterpret_cast<struct sockaddr *>(&send_info.to),
            send_info.to_len); 

        settings_.qs->Send(buffer, dest_endpoint);
        sent = true;

        buffer = settings_.qs->allocator_.Allocate();
    }

    TickTimeout();

    return sent;
}

void QuicheConnection::ProcessH3Events() {
    // Called from function with lock held

    for (;;) {
        quiche_h3_event* ev = nullptr;
        int64_t stream_id = quiche_h3_conn_poll(http3_, conn_, &ev);
        if (stream_id == QUICHE_ERR_DONE) {
            break;
        } else if (stream_id < 0) {
            LOG_ERROR() << "quiche_h3_conn_poll failed: " << stream_id << " " << quiche_h3_error_to_string(stream_id);
            break;
        }
        CallbackScope ev_scope([ev]() { quiche_h3_event_free(ev); });

        auto event_type = quiche_h3_event_type(ev);
        switch (event_type) {
            case QUICHE_H3_EVENT_HEADERS: {
                //LOG_INFO() << "Received headers: stream_id=" << stream_id;
                auto stream = GetIncomingStream(stream_id);

                using cb_t = std::function<bool(const std::string&, const std::string&)>;
                cb_t cb = [this, stream](const std::string& name, const std::string& value) -> bool {
                    stream->OnHeader(name, value);
                    //LOG_INFO() << "Header: " << name << ": " << value;
                    return true; // Return false to stop iterating
                };

                // C++ adapter for C callback
                auto ccb = [](uint8_t* name, size_t name_len,
                                    uint8_t* value, size_t value_len,
                                    void* argp) -> int {
                    cb_t* cb = reinterpret_cast<cb_t*>(argp);
                    if (!(*cb)(std::string(reinterpret_cast<char*>(name), name_len),
                                std::string(reinterpret_cast<char*>(value), value_len))) {
                        return -1;
                    }
                    return 0;
                };
                quiche_h3_event_for_each_header(ev, ccb, &cb);
                break;
            }

            case QUICHE_H3_EVENT_DATA: {
                auto stream = GetIncomingStream(stream_id);

                for (;;) {
                    auto& buffer = settings_.qs->body_buf_;
                    ssize_t len = quiche_h3_recv_body(
                        http3_,
                        conn_,
                        stream_id,
                        buffer.data(),
                        buffer.size());
                    if (len == QUICHE_ERR_DONE || len == 0) {
                        break;
                    }
                    if (len > 0) {
                        stream->OnData(buffer.data(), len);
                    } else {
                        LOG_ERROR() << "*** quiche_h3_recv_body failed: " << len << " " << quiche_error_to_string(len);
                    }
                }
                break;
            }

            case QUICHE_H3_EVENT_FINISHED: {
                //LOG_INFO() << "QUICHE_H3_EVENT_FINISHED: stream_id=" << stream_id;
                std::shared_ptr<IncomingStream> stream;

                auto it = incoming_streams_by_id_.find(stream_id);
                if (it != incoming_streams_by_id_.end()) {
                    stream = it->second;
                } else {
                    break; // Ignore FINISHED events for streams that have been destroyed
                }
                incoming_streams_by_id_.erase(it);

                QuicheMailbox::Event event;
                event.Type = QuicheMailbox::EventType::Data;
                event.PeerEndpoint = peer_endpoint_;
                event.ConnectionAssignedId = settings_.AssignedId;
                event.Stream = stream;

                settings_.on_data(event);

                // After client gets a response, destroy the stream
                if (!settings_.IsServer) {
                    DestroyStream(stream_id);
                }
                break;
            }

            case QUICHE_H3_EVENT_RESET: {
                //LOG_INFO() << "QUICHE_H3_EVENT_RESET: stream_id=" << stream_id;
                DestroyStream(stream_id);
                break;
            }

            case QUICHE_H3_EVENT_PRIORITY_UPDATE:
                break;

            case QUICHE_H3_EVENT_GOAWAY: {
                // Note: stream_id is invalid here:
                // The event GoAway returns an ID that depends on the connection role. A client receives the largest processed stream ID. A server receives the the largest permitted push ID.
                const char* reason = "Received GOAWAY";
                LOG_INFO() << "Connection aborted: " << reason;
                quiche_conn_close(conn_, true, 0, (const uint8_t*)reason, strlen(reason));
                // FIXME: Better way to do this?
                break;
            }

            default:
                //LOG_INFO() << "Unknown event type: " << event_type;
                break;
        }
    }
}

std::shared_ptr<IncomingStream> QuicheConnection::GetIncomingStream(uint64_t stream_id, bool create) {
    // Called from function with lock held

    auto it = incoming_streams_by_id_.find(stream_id);
    if (it != incoming_streams_by_id_.end()) {
        return it->second;
    }

    if (!create) {
        return nullptr;
    }

    auto stream = std::make_shared<IncomingStream>();
    stream->Id = stream_id;
    incoming_streams_by_id_[stream_id] = stream;
    return stream;
}

std::shared_ptr<OutgoingStream> QuicheConnection::GetOutgoingStream(uint64_t stream_id, bool create) {
    // Called from function with lock held

    auto it = outgoing_streams_by_id_.find(stream_id);
    if (it != outgoing_streams_by_id_.end()) {
        return it->second;
    }

    if (!create) {
        return nullptr;
    }

    auto stream = std::make_shared<OutgoingStream>();
    stream->Id = stream_id;
    outgoing_streams_by_id_[stream_id] = stream;
    return stream;
}

void QuicheConnection::DestroyStream(uint64_t stream_id) {
    // Called from function with lock held

    quiche_conn_stream_shutdown(conn_, stream_id, QUICHE_SHUTDOWN_READ, 0);
    quiche_conn_stream_shutdown(conn_, stream_id, QUICHE_SHUTDOWN_WRITE, 0);

    auto it = incoming_streams_by_id_.find(stream_id);
    if (it != incoming_streams_by_id_.end()) {
        incoming_streams_by_id_.erase(it);
    }

    auto ot = outgoing_streams_by_id_.find(stream_id);
    if (ot != outgoing_streams_by_id_.end()) {
        outgoing_streams_by_id_.erase(ot);
    }
}

int64_t QuicheConnection::SendRequest(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const void* data,
    int bytes)
{
    if (timeout_) {
        return -1;
    }

    std::vector<quiche_h3_header> h3_headers;
    for (const auto& header : headers) {
        h3_headers.push_back(quiche_h3_header{
            reinterpret_cast<const uint8_t*>(header.first.c_str()),
            header.first.length(),
            reinterpret_cast<const uint8_t*>(header.second.c_str()),
            header.second.length()
        });
    }

    while (!timeout_) {
        // Hold lock while sending request
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);

            int64_t stream_id = quiche_h3_send_request(
                http3_,
                conn_,
                h3_headers.data(),
                h3_headers.size(),
                (bytes <= 0)/*fin*/);

            // If request is blocked by flow control:
            if ((stream_id == QUICHE_H3_ERR_STREAM_BLOCKED || stream_id == QUICHE_H3_TRANSPORT_ERR_STREAM_LIMIT)
                && quiche_conn_is_established(conn_)) { 
                // Sleep for a while to allow flow control to drain and retry (below)
                //LOG_INFO() << "Request blocked by flow control, retrying";
            } else {
                if (stream_id < 0) {
                    LOG_ERROR() << "failed to send request: " << stream_id << " " << quiche_h3_error_to_string(stream_id);
                    return -1;
                }

                SendBody(stream_id, data, bytes);
                return stream_id;
            }
        }

        // Wait for flow control to drain
        std::this_thread::sleep_for(std::chrono::milliseconds(QUIC_SEND_SLOW_INTERVAL_MSEC));
    } // Retry

    return -1;
}

bool QuicheConnection::SendResponse(
    uint64_t stream_id,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const void* data,
    int bytes)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (timeout_) {
        return false;
    }

    // Convert headers to quiche_h3_header format
    std::vector<quiche_h3_header> h3_headers;
    for (const auto& header : headers) {
        h3_headers.emplace_back(quiche_h3_header{
            reinterpret_cast<const uint8_t*>(header.first.c_str()),
            header.first.length(),
            reinterpret_cast<const uint8_t*>(header.second.c_str()),
            header.second.length()
        });
    }

    // Attempt to send the response headers
    int r = quiche_h3_send_response(
        http3_, conn_,
        stream_id,
        h3_headers.data(), h3_headers.size(),
        (bytes <= 0) /* fin */);

    if (r == QUICHE_H3_ERR_STREAM_BLOCKED && quiche_conn_is_established(conn_)) {
        // Flow control is blocking the send, cache the response
        auto cached_response = std::make_shared<CachedResponse>();
        cached_response->stream_id = stream_id;
        cached_response->headers = std::move(h3_headers);
        if (bytes > 0 && data != nullptr) {
            // Copy the body data into a shared_ptr vector
            cached_response->data = std::make_shared<std::vector<uint8_t>>(
                reinterpret_cast<const uint8_t*>(data),
                reinterpret_cast<const uint8_t*>(data) + bytes
            );
        }
        cached_response->bytes_left = bytes;

        // Add the cached response to the cache
        response_cache_.push_back(cached_response);
        return false; // Indicate that the response was cached
    } else if (r < 0) {
        LOG_ERROR() << "Failed to send response headers: " << r << " " << quiche_h3_error_to_string(r);
        return false;
    }

    // Headers sent successfully, now send the body
    return SendBody(stream_id, data, bytes);
}

bool QuicheConnection::SendBody(uint64_t stream_id, const void* vdata, int bytes) {
    // Called from function with lock held

    if (bytes <= 0) {
        //quiche_conn_stream_shutdown(conn_, stream_id, QUICHE_SHUTDOWN_WRITE, 0);
    } else {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);
        ssize_t rc = quiche_h3_send_body(http3_, conn_, stream_id, 
                                 data, bytes,
                                 false/*fin*/);
        if (rc < 0) {
            // Failures here mean there is no room for more data.
            // Instead just queue all the data and send it later.
            rc = 0;
        }

        if (rc < bytes) {
            auto stream = GetOutgoingStream(stream_id);
            stream->SendOffset = 0;
            stream->Buffer.assign(data + rc, data + bytes);
        } else {
            rc = quiche_h3_send_body(http3_, conn_, stream_id,
                                    nullptr, 0/*empty*/,
                                    true/*fin*/);
            if (rc < 0) {
                LOG_ERROR() << "Failed to send body(0): " << rc;
                return false;
            }
        }
    }

    FlushEgress();
    return true;
}

void QuicheConnection::FlushCachedResponses() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Iterate over the cached responses
    for (auto it = response_cache_.begin(); it != response_cache_.end(); ) {
        auto cached_response = *it;

        // Attempt to resend the response headers
        int r = quiche_h3_send_response(
            http3_, conn_,
            cached_response->stream_id,
            cached_response->headers.data(), cached_response->headers.size(),
            cached_response->bytes_left <= 0 /* fin */);

        if (r == QUICHE_H3_ERR_STREAM_BLOCKED && quiche_conn_is_established(conn_)) {
            // Still blocked, skip to next cached response
            ++it;
            continue;
        } else if (r < 0) {
            LOG_ERROR() << "Failed to resend cached response headers: " << r << " " << quiche_h3_error_to_string(r);
            // Remove the failed cached response
            it = response_cache_.erase(it);
            continue;
        }

        // If there is body data to send
        if (cached_response->data && cached_response->bytes_left > 0) {
            ssize_t sent = quiche_h3_send_body(
                http3_, conn_,
                cached_response->stream_id,
                cached_response->data->data(),
                cached_response->bytes_left,
                (cached_response->bytes_left <= 0) /* fin */);
            if (sent == QUICHE_H3_ERR_DONE) {
                it = response_cache_.erase(it);
                continue;
            }

            if (sent == QUICHE_H3_ERR_STREAM_BLOCKED) {
                // Still blocked, cannot send now
                ++it;
                continue;
            } else if (sent < 0) {
                LOG_ERROR() << "Failed to resend cached response body: " << sent << " " << quiche_h3_error_to_string(sent);
                // Remove the failed cached response
                it = response_cache_.erase(it);
                continue;
            }

            // Update the bytes left to send
            cached_response->bytes_left -= sent;

            if (cached_response->bytes_left <= 0) {
                // All data sent, remove from cache
                it = response_cache_.erase(it);
            } else {
                ++it;
            }
        } else {
            // No body to send, remove from cache
            it = response_cache_.erase(it);
        }
    }
}

void QuicheConnection::FlushTransfers() {
    // Called from function with lock held

    std::vector<uint64_t> completed_stream_ids;

    for (auto& stream_pair : outgoing_streams_by_id_) {
        auto& stream = stream_pair.second;
        const int send_offset = stream->SendOffset;
        const int remaining = stream->Buffer.size() - send_offset;

        // If we are retrying to send the FIN:
        if (remaining <= 0) {
            ssize_t r = quiche_h3_send_body(http3_, conn_, stream->Id,
                                    nullptr, 0/*empty*/,
                                    true/*fin*/);
            if (r < 0) {
                // Failures here mean there is no room for more data
                break;
            }

            completed_stream_ids.push_back(stream->Id);
            continue;
        }

        ssize_t r = quiche_h3_send_body(http3_, conn_, stream->Id,
                                 stream->Buffer.data() + send_offset,
                                 remaining,
                                 false/*fin*/);
        if (r < 0) {
            // Failures here mean there is no room for more data
            break;
        }

        if (r < remaining) {
            stream->SendOffset += r;
            continue;
        }

        // No more data to send
        stream->Buffer.clear();
        stream->SendOffset = 0;

        // Try to send FIN
        r = quiche_h3_send_body(http3_, conn_, stream->Id,
                                nullptr, 0/*empty*/,
                                true/*fin*/);
        if (r < 0) {
            // Failures here mean there is no room for more data
            break;
        }

        completed_stream_ids.push_back(stream->Id);

        //quiche_conn_stream_shutdown(conn_, stream_id, QUICHE_SHUTDOWN_WRITE, 0);
    }

    // Erase completed streams
    for (auto stream_id : completed_stream_ids) {
        auto it = outgoing_streams_by_id_.find(stream_id);
        if (it != outgoing_streams_by_id_.end()) {
            outgoing_streams_by_id_.erase(it);
        }
    }
}

bool QuicheConnection::ComparePeerCertificate(const void* cert_cer_data, int bytes) {
    std::lock_guard<std::recursive_mutex> locker(mutex_);

    const uint8_t* peer_cert = nullptr;
    size_t peer_cert_len = 0;
    quiche_conn_peer_cert(conn_, &peer_cert, &peer_cert_len);

    if ((int)peer_cert_len != bytes ||
        memcmp(peer_cert, cert_cer_data, peer_cert_len) != 0)
    {
        const char* reason = "Peer certificate does not match";
        LOG_ERROR() << "Connection aborted: " << reason;
        quiche_conn_close(conn_, true, 0, (const uint8_t*)reason, strlen(reason));
        return false;
    }

    connected_ = true;

    return true;
}


//------------------------------------------------------------------------------
// QuicheSender

QuicheSender::QuicheSender(std::shared_ptr<QuicheSocket> qs) {
    qs_ = qs;

    send_thread_ = std::make_shared<std::thread>([this]() {
        Loop();
    });
}

QuicheSender::~QuicheSender() {
    terminated_ = true;
    JoinThread(send_thread_);
}

void QuicheSender::Loop() {
    auto buffer = qs_->allocator_.Allocate();

    int interval_msec = QUIC_SEND_SLOW_INTERVAL_MSEC;

    while (!terminated_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_msec));

        std::vector<std::shared_ptr<QuicheConnection>> freed_connections;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            bool send_fast = false;

            auto it = connections_.begin();
            while (it != connections_.end()) {
                auto& connection = it->second;
                if (connection->IsClosed()) {
                    freed_connections.push_back(connection);

                    // Remove from connections_
                    it = connections_.erase(it);

                    // Remove from connections_by_id_
                    auto conn_it = connections_by_id_.find(connection->settings_.AssignedId);
                    if (conn_it != connections_by_id_.end()) {
                        connections_by_id_.erase(conn_it);
                    }
                } else {
                    if (connection->FlushEgress(buffer)) {
                        send_fast = true;
                    }
                    it++;
                }
            }

            if (send_fast) {
                interval_msec = QUIC_SEND_FAST_INTERVAL_MSEC;
            } else {
                interval_msec = QUIC_SEND_SLOW_INTERVAL_MSEC;
            }
        }

        // Release connections outside of the lock
        freed_connections.clear();
    }
}

void QuicheSender::Add(std::shared_ptr<QuicheConnection> qc) {

    ConnectionId dcid = qc->settings_.dcid;
    uint64_t connection_id = qc->settings_.AssignedId;

    std::lock_guard<std::mutex> lock(mutex_);

    connections_[dcid] = qc;
    connections_by_id_[connection_id] = qc;
}

std::shared_ptr<QuicheConnection> QuicheSender::Find(const ConnectionId& dcid) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto conn_it = connections_.find(dcid);
    if (conn_it == connections_.end()) {
        return nullptr;
    }
    return conn_it->second;
}

std::shared_ptr<QuicheConnection> QuicheSender::Find(uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto conn_it = connections_by_id_.find(connection_id);
    if (conn_it == connections_by_id_.end()) {
        return nullptr;
    }
    return conn_it->second;
}


//------------------------------------------------------------------------------
// QuicheMailbox

void QuicheMailbox::Shutdown()
{
    std::unique_lock<std::mutex> lock(mutex_);
    terminated_ = true;
    cv_.notify_one();
}

void QuicheMailbox::Poll(MailboxCallback callback, int timeout_msec)
{
    std::vector<Event> events;
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (timeout_msec < 0) {
            cv_.wait(lock, 
                [this] { return terminated_ || !events_.empty(); });
        } else {
            cv_.wait_for(lock, std::chrono::milliseconds(timeout_msec),
                [this] { return terminated_ || !events_.empty(); });
        }

        if (terminated_ || events_.empty()) {
            return;
        }

        std::swap(events, events_);
    }

    // Process events without lock held to avoid deadlock and blocking IO thread
    for (const auto& event : events) {
        callback(event);
    }
}

void QuicheMailbox::Post(const Event& event)
{
    std::unique_lock<std::mutex> lock(mutex_);

    events_.push_back(event);
    cv_.notify_one();
}
