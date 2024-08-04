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
            LOG_INFO() << "OnConnect: cid=" << connection_id << " addr=" << peer_endpoint;
        };
        auto OnTimeout = [](uint64_t connection_id) {
            LOG_INFO() << "OnTimeout: cid=" << connection_id;
        };
        auto OnRequest = [](PythonRequest request) { 
            LOG_INFO() << "OnRequest: cid=" << request.ConnectionAssignedId << " rid=" << request.RequestId << " path=" << request.Path << " ct=" << request.Body.ContentType << " len=" << request.Body.Length;

            std::vector<char> response(16*1024*1024, 'A');

            PythonBody body{};
            body.ContentType = "text/plain";
            body.Data = (const uint8_t*)response.data();
            body.Length = (int32_t)response.size();
            quicsend_server_respond(m_server, request.ConnectionAssignedId, request.RequestId, 200, &body);

            PythonBody body2{};
            quicsend_server_request(m_server, request.ConnectionAssignedId, "simple2.txt", &body2);
        };
        auto OnResponse = [](PythonResponse response) {
            LOG_INFO() << "OnResponse: cid=" << response.ConnectionAssignedId << " rid=" << response.RequestId << " status=" << response.Status << " ct=" << response.Body.ContentType << " len=" << response.Body.Length;
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
