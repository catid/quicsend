#include <quicsend_client.hpp>
#include <quicsend_server.hpp>

extern "C" {


//------------------------------------------------------------------------------
// C API

struct PythonRequestData {
    uint64_t ConnectionId;
    bool IsResponse;
    int64_t RequestId;

    const char* Path;
    const char* ContentType;
    const uint8_t* Data;
    int32_t Length;
};

typedef void (*connect_callback)(uint64_t connection_id, const char* peer_endpoint);
typedef void (*timeout_callback)(uint64_t connection_id);
typedef void (*data_callback)(PythonRequestData data);


//------------------------------------------------------------------------------
// C API : QuicSendClient

struct PythonClientSettings {
    const char* Host;
    uint16_t Port;
    const char* CertPath;
};

QuicSendClient* quicsend_client_create(const PythonClientSettings* settings)
{
    QuicSendClientSettings cs;
    cs.Host = settings->Host;
    cs.Port = settings->Port;
    cs.CertPath = settings->CertPath;

    return new QuicSendClient(cs);
}

void quicsend_client_destroy(QuicSendClient *client) {
    if (client != NULL) {
        delete client;
    }
}

int64_t quicsend_client_request(
    QuicSendClient *client,
    const char* path,
    const char* content_type,
    const void* data,
    int32_t bytes)
{
    if (client == NULL) {
        return -1;
    }

    return client->Request(path, BodyDataTypeFromString(content_type), data, bytes);
}

void quicsend_client_poll(QuicSendClient *client,
                          connect_callback on_connect,
                          timeout_callback on_timeout,
                          data_callback on_data,
                          int32_t timeout_msec) {
    if (client == NULL) {
        return;
    }

    auto fn_connect = [&](uint64_t connection_id, const boost::asio::ip::udp::endpoint& peer_endpoint) {
        if (!on_connect) {
            return;
        }

        std::string peer_endpoint_str = peer_endpoint.address().to_string();

        on_connect(connection_id, peer_endpoint_str.c_str());
    };
    auto fn_timeout = [&](uint64_t connection_id) {
        if (!on_timeout) {
            return;
        }

        on_timeout(connection_id);
    };
    auto fn_data = [&](uint64_t connection_id, const QuicheMailbox::Event& event) {
        if (!on_data) {
            return;
        }
        std::string content_type = BodyDataTypeToString(event.Type);

        PythonRequestData data;
        data.ConnectionId = connection_id;
        data.RequestId = event.Id;
        data.IsResponse = event.IsResponse;

        data.Path = event.Path.c_str();
        data.ContentType = content_type.c_str();
        if (!event.Buffer) {
            data.Data = nullptr;
            data.Length = 0;
        } else {
            data.Data = event.Buffer->data();
            data.Length = event.Buffer->size();
        }

        on_data(data);
    };

    client->Poll(fn_connect, fn_timeout, fn_data, timeout_msec);
}


//------------------------------------------------------------------------------
// C API : QuicSendServer

struct PythonServerSettings {
    const char* Host;
    uint16_t Port;
    const char* CertPath;
};

QuicSendServer* quicsend_server_create(const QuicSendServerSettings* settings)
{
    QuicSendServerSettings ss;
    ss.Port = settings->Port;
    ss.KeyPath = settings->KeyPath;
    ss.CertPath = settings->CertPath;

    return new QuicSendServer(ss);
}

void quicsend_server_destroy(QuicSendServer *client) {
    if (client != NULL) {
        delete client;
    }
}

int64_t quicsend_server_request(
    QuicSendServer *client,
    const char* path,
    const char* content_type,
    const void* data,
    int32_t bytes)
{
    if (client == NULL) {
        return;
    }

    return client->Request(path, BodyDataTypeFromString(content_type), data, bytes);
}

void quicsend_server_poll(QuicSendServer *client,
                          connect_callback on_connect,
                          timeout_callback on_timeout,
                          data_callback on_data,
                          int32_t timeout_msec) {
    if (client == NULL) {
        return;
    }

    auto fn_connect = [&](uint64_t connection_id, const boost::asio::ip::udp::endpoint& peer_endpoint) {
        if (!on_connect) {
            return;
        }

        std::string peer_endpoint_str = peer_endpoint.address().to_string();

        on_connect(connection_id, peer_endpoint_str.c_str());
    };
    auto fn_timeout = [&](uint64_t connection_id) {
        if (!on_timeout) {
            return;
        }

        on_timeout(connection_id);
    };
    auto fn_data = [&](uint64_t connection_id, const QuicheMailbox::Event& event) {
        if (!on_data) {
            return;
        }
        std::string content_type = BodyDataTypeToString(event.Type);

        PythonRequestData data;
        data.ConnectionId = connection_id;
        data.RequestId = event.Id;
        data.IsResponse = event.IsResponse;

        data.Path = event.Path.c_str();
        data.ContentType = content_type.c_str();
        if (!event.Buffer) {
            data.Data = nullptr;
            data.Length = 0;
        } else {
            data.Data = event.Buffer->data();
            data.Length = event.Buffer->size();
        }

        on_data(data);
    };

    client->Poll(fn_connect, fn_timeout, fn_data, timeout_msec);
}


} // extern "C"
