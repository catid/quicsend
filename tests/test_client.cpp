#include <quicsend_python.h>


//------------------------------------------------------------------------------
// CTRL+C

#include <csignal>
#include <atomic>

static std::atomic<bool> running(true);

static void signalHandler(int signum) {
    LOG_INFO() << "Interrupt signal (" << signum << ") received.";
    running = false;
}


//------------------------------------------------------------------------------
// Entrypoint

QuicSendClient* m_client = nullptr;

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
        settings.Host = host.c_str();
        settings.Port = port;
        settings.CertPath = cert_path.c_str();

        m_client = quicsend_client_create(&settings);

        auto OnConnect = [](uint64_t connection_id, const char* peer_endpoint) {
            LOG_INFO() << "OnConnect: " << connection_id << " " << peer_endpoint;

            quicsend_client_request(m_client, "simple.txt", nullptr, nullptr, 0);
        };
        auto OnTimeout = [](uint64_t connection_id) {
            LOG_INFO() << "OnTimeout: " << connection_id;
        };
        auto OnData = [](PythonRequestData data) {
            LOG_INFO() << "OnData: " << data.ConnectionAssignedId << " " << data.RequestId << " " << data.b_IsResponse << " " << data.Path << " " << data.ContentType << " " << data.Length;
        };

        for (;;) {
            int32_t r = quicsend_client_poll(m_client, OnConnect, OnTimeout, OnData, 100);

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
