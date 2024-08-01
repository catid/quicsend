#include <quicsend_client.hpp>

//------------------------------------------------------------------------------
// Entrypoint

bool QuicSendClient::SendExampleRequest() {
    std::string data(1024 * 1024 * 1024, 'A');

    const std::vector<std::pair<std::string, std::string>> headers = {
        {":method", "PUT"},
        {":scheme", "https"},
        {":authority", settings_.Host},
        {":path", "/"},
        {"user-agent", "quiche"},
        {"content-type", "application/octet-stream"},
        {"content-length", std::to_string(data.size())}
    };

    auto fn = [this](DataStream& stream) {
        LOG_INFO() << "*** [" << stream.Id << "] Response " << stream.Buffer->size() << " bytes";
    };

    return connection_->SendRequest(fn, headers, data.c_str(), data.size());
}

int main(int argc, char* argv[]) {
#ifdef ENABLE_QUICHE_DEBUG_LOGGING
    EnableQuicheDebugLogging();
#endif // ENABLE_QUICHE_DEBUG_LOGGING

    const char* host = argc >= 2 ? argv[1] : "localhost";
    const char* port_str = argc >= 3 ? argv[2] : "4433";
    uint16_t port = std::stoi(port_str);

    try {
        QuicSendClientSettings settings;
        settings.Host = host;
        settings.Port = port;
        settings.CertPath = "server.pem";

        QuicSendClient client(settings);

        auto OnTimeout = []() {
            LOG_INFO() << "*** Connection timed out";
        };
        auto OnConnect = []() {
            LOG_INFO() << "*** Connection established";
        };
        auto OnRequest = [](DataStream& stream) {
            LOG_INFO() << "*** [" << stream.Id << "] Received " << stream.Method
                << ": " << stream.Path << " " << stream.Buffer->size() << " bytes";
        };

        while (client.IsRunning()) {
            client.Poll(OnConnect, OnTimeout, OnRequest);
        }
    } catch (std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
        return -1;
    }

    return 0;
}
