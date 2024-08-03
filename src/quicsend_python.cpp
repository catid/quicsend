#include <quicsend_python.h>

extern "C" {


//------------------------------------------------------------------------------
// C API : QuicSendClient

QuicSendClient* quicsend_client_create(const PythonQuicSendClientSettings* settings)
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

int32_t quicsend_client_poll(QuicSendClient *client,
                          connect_callback on_connect,
                          timeout_callback on_timeout,
                          data_callback on_data,
                          int32_t timeout_msec) {
    if (client == NULL || !client->IsRunning()) {
        return 0;
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
    auto fn_data = [&](const QuicheMailbox::Event& event) {
        if (!on_data) {
            return;
        }
        std::string content_type = BodyDataTypeToString(event.Type);

        PythonRequestData data;
        data.ConnectionAssignedId = event.ConnectionAssignedId;
        data.RequestId = event.Id;
        data.b_IsResponse = event.IsResponse ? 1 : 0;

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

    return 1;
}


//------------------------------------------------------------------------------
// C API : QuicSendServer

QuicSendServer* quicsend_server_create(const PythonQuicSendServerSettings* settings)
{
    QuicSendServerSettings ss;
    ss.Port = settings->Port;
    ss.KeyPath = settings->KeyPath;
    ss.CertPath = settings->CertPath;

    return new QuicSendServer(ss);
}

void quicsend_server_destroy(QuicSendServer *server) {
    if (server != NULL) {
        delete server;
    }
}

int64_t quicsend_server_request(
    QuicSendServer *server,
    uint64_t connection_id,
    const char* path,
    const char* content_type,
    const void* data,
    int32_t bytes)
{
    if (server == NULL) {
        return -1;
    }

    return server->Request(connection_id, path, BodyDataTypeFromString(content_type), data, bytes);
}

int32_t quicsend_server_poll(QuicSendServer *server,
                          connect_callback on_connect,
                          timeout_callback on_timeout,
                          data_callback on_data,
                          int32_t timeout_msec) {
    if (server == NULL || !server->IsRunning()) {
        return 0;
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
    auto fn_data = [&](const QuicheMailbox::Event& event) {
        if (!on_data) {
            return;
        }
        std::string content_type = BodyDataTypeToString(event.Type);

        PythonRequestData data;
        data.ConnectionAssignedId = event.ConnectionAssignedId;
        data.RequestId = event.Id;
        data.b_IsResponse = event.IsResponse ? 1 : 0;

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

    server->Poll(fn_connect, fn_timeout, fn_data, timeout_msec);
    return 1;
}


} // extern "C"
