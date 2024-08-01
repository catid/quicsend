#include <quicsend_server.hpp>


//------------------------------------------------------------------------------
// Entrypoint

int main(int argc, char* argv[]) {
#ifdef ENABLE_QUICHE_DEBUG_LOGGING
    EnableQuicheDebugLogging();
#endif // ENABLE_QUICHE_DEBUG_LOGGING

    std::string port_str = argc >= 2 ? argv[1] : "4433";
    uint16_t port = std::stoi(port_str);
    std::string cert_path = argc >= 3 ? argv[2] : "server.pem";
    std::string key_path = argc >= 4 ? argv[3] : "server.key";

    try {
        QuicSendServer server(port, cert_path, key_path);

        while (server.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    catch (std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what() << "";
        return 1;
    }

    return 0;
}
