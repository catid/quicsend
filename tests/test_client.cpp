#include <quicsend_python.h>


//------------------------------------------------------------------------------
// CTRL+C

#include <csignal>
#include <atomic>

static std::atomic<bool> m_terminated = ATOMIC_VAR_INIT(false);

static void signalHandler(int signum) {
    LOG_INFO() << "Interrupt signal (" << signum << ") received.";
    m_terminated = true;
}


//------------------------------------------------------------------------------
// Entrypoint

QuicSendClient* m_client = nullptr;
uint64_t m_t0 = 0;

int main(int argc, char* argv[]) {
#ifdef ENABLE_QUICHE_DEBUG_LOGGING
    EnableQuicheDebugLogging();
#endif // ENABLE_QUICHE_DEBUG_LOGGING

    std::string host = argc >= 2 ? argv[1] : "localhost";
    std::string port_str = argc >= 3 ? argv[2] : "4433";
    uint16_t port = std::stoi(port_str);
    std::string cert_path = argc >= 4 ? argv[3] : "server.pem";

    std::signal(SIGINT, signalHandler);

    try {
        PythonQuicSendClientSettings settings;
        settings.AuthToken = "AUTH_TOKEN_PLACEHOLDER";
        settings.Host = host.c_str();
        settings.Port = port;
        settings.CertPath = cert_path.c_str();

        m_client = quicsend_client_create(&settings);

        auto OnConnect = [](uint64_t connection_id, const char* peer_endpoint) {
            LOG_INFO() << "OnConnect: cid=" << connection_id << " addr=" << peer_endpoint;

            m_t0 = GetNsec();

            PythonBody body{};
            int64_t rid = quicsend_client_request(m_client, "simple.txt", "{\"foo\": \"bar\"}", &body);
            LOG_INFO() << "Send request id=" << rid;
        };
        auto OnTimeout = [](uint64_t connection_id) {
            LOG_INFO() << "OnTimeout: cid=" << connection_id;
        };
        auto OnResponse = [](PythonResponse response) {
            uint64_t t1 = GetNsec();
            LOG_INFO() << "Throughput: " << response.Body.Length * 1000.0 / (t1 - m_t0) << " MB/s";

            LOG_INFO() << "OnResponse: cid=" << response.ConnectionAssignedId << " rid=" << response.RequestId
                << " hinfo=" << response.HeaderInfo << " status=" << response.Status
                << " ct=" << response.Body.ContentType << " len=" << response.Body.Length; 

            m_t0 = GetNsec();

            PythonBody body{};
            int64_t rid = quicsend_client_request(m_client, "simple.txt", "{\"foo\": \"bar\"}", &body);
            LOG_INFO() << "Send request id=" << rid;
        };

        while (!m_terminated) {
            int32_t r = quicsend_client_poll(m_client, OnConnect, OnTimeout, OnResponse, 100);

            if (r == 0) {
                break;
            }
        }

        quicsend_client_destroy(m_client);
    } catch (std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
        return -1;
    }

    return 0;
}
