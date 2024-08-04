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

QuicSendServer* m_server = nullptr;

int main(int argc, char* argv[]) {
#ifdef ENABLE_QUICHE_DEBUG_LOGGING
    EnableQuicheDebugLogging();
#endif // ENABLE_QUICHE_DEBUG_LOGGING

    std::string port_str = argc >= 2 ? argv[1] : "4433";
    uint16_t port = std::stoi(port_str);
    std::string cert_path = argc >= 3 ? argv[2] : "server.pem";
    std::string key_path = argc >= 4 ? argv[3] : "server.key";

    std::signal(SIGINT, signalHandler);

    try {
        PythonQuicSendServerSettings settings;
        settings.AuthToken = "AUTH_TOKEN_PLACEHOLDER";
        settings.Port = port;
        settings.CertPath = cert_path.c_str();
        settings.KeyPath = key_path.c_str();

        m_server = quicsend_server_create(&settings);

        auto OnConnect = [](uint64_t connection_id, const char* peer_endpoint) {
            LOG_INFO() << "OnConnect: " << connection_id << " " << peer_endpoint;
        };
        auto OnTimeout = [](uint64_t connection_id) {
            LOG_INFO() << "OnTimeout: " << connection_id;
        };
        auto OnRequest = [](PythonRequest request) { 
            LOG_INFO() << "OnRequest: " << request.ConnectionAssignedId << " " << request.RequestId << " " << request.Path << " " << request.Body.ContentType << " " << request.Body.Length;
        };
        auto OnResponse = [](PythonResponse response) {
            LOG_INFO() << "OnResponse: " << response.ConnectionAssignedId << " " << response.RequestId << " " << response.Status << " " << response.Body.ContentType << " " << response.Body.Length;
        };

        while (!m_terminated) {
            int32_t r = quicsend_server_poll(m_server, OnConnect, OnTimeout, OnRequest, OnResponse, 100);

            if (r == 0) {
                break;
            }
        }

        quicsend_server_destroy(m_server);
    }
    catch (std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what() << "";
        return 1;
    }

    return 0;
}
