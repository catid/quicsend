#include <quicsend_python.h>

//------------------------------------------------------------------------------
// Tools

static void route_data(const QuicheMailbox::Event& event, request_callback on_request, response_callback on_response) {
    std::string content_type = BodyDataTypeToString(event.Type);

    if (event.IsResponse)
    {
        PythonResponse response;
        response.ConnectionAssignedId = event.ConnectionAssignedId;
        response.RequestId = event.Id;

        response.Status = std::atoi(event.Status.c_str());
        response.Body.ContentType = content_type.c_str();
        if (!event.Buffer) {
            response.Body.Data = nullptr;
            response.Body.Length = 0;
        } else {
            response.Body.Data = event.Buffer->data();
            response.Body.Length = event.Buffer->size();
        }

        if (on_response) {
            on_response(response);
        }
    }
    else
    {
        PythonRequest request;
        request.ConnectionAssignedId = event.ConnectionAssignedId;
        request.RequestId = event.Id;

        request.Path = event.Path.c_str();
        request.Body.ContentType = content_type.c_str();
        if (!event.Buffer) {
            request.Body.Data = nullptr;
            request.Body.Length = 0;
        } else {
            request.Body.Data = event.Buffer->data();
            request.Body.Length = event.Buffer->size();
        }

        if (on_request) {
            on_request(request);
        }
    }
}

BodyData PythonBodyToBodyData(const PythonBody* body) {
    BodyData bd;
    bd.ContentType = body->ContentType;
    bd.Data = body->Data;
    bd.Length = body->Length;
    return bd;
}

extern "C" {


//------------------------------------------------------------------------------
// C API : QuicSendClient

QuicSendClient* quicsend_client_create(const PythonQuicSendClientSettings* settings)
{
    QuicSendClientSettings cs;
    cs.Authorization = std::string("Bearer ") + settings->AuthToken;
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
    const PythonBody* body)
{
    if (client == NULL) {
        return -1;
    }

    BodyData bd = PythonBodyToBodyData(body);
    return client->Request(path, bd);
}

int32_t quicsend_client_poll(
    QuicSendClient *client,
    connect_callback on_connect,
    timeout_callback on_timeout,
    request_callback on_request,
    response_callback on_response,
    int32_t timeout_msec)
{
    if (client == NULL || !client->IsRunning()) {
        return 0;
    }

    auto fn_connect = [&](uint64_t connection_id, const boost::asio::ip::udp::endpoint& peer_endpoint) {
        std::string addr_str = EndpointToString(peer_endpoint);

        if (on_connect) {
            on_connect(connection_id, addr_str.c_str());
        }
    };
    auto fn_timeout = [&](uint64_t connection_id) {
        if (on_timeout) {
            on_timeout(connection_id);
        }
    };
    auto fn_data = [&](const QuicheMailbox::Event& event) {
        route_data(event, on_request, on_response);
    };

    client->Poll(fn_connect, fn_timeout, fn_data, timeout_msec);

    return 1;
}

void quicsend_client_respond(
    QuicSendClient* client,
    int64_t request_id,
    int32_t status,
    const PythonBody* body)
{
    if (!client || !client->IsRunning()) {
        return;
    }

    BodyData bd = PythonBodyToBodyData(body);
    client->Respond(request_id, status, bd);
}


//------------------------------------------------------------------------------
// C API : QuicSendServer

QuicSendServer* quicsend_server_create(const PythonQuicSendServerSettings* settings)
{
    QuicSendServerSettings ss;
    ss.Authorization = std::string("Bearer ") + settings->AuthToken;
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
    const PythonBody* body)
{
    if (server == NULL) {
        return -1;
    }

    BodyData bd = PythonBodyToBodyData(body);
    return server->Request(connection_id, path, bd);
}

int32_t quicsend_server_poll(
    QuicSendServer *server,
    connect_callback on_connect,
    timeout_callback on_timeout,
    request_callback on_request,
    response_callback on_response,
    int32_t timeout_msec)
{
    if (server == NULL || !server->IsRunning()) {
        return 0;
    }

    auto fn_connect = [&](uint64_t connection_id, const boost::asio::ip::udp::endpoint& peer_endpoint) {
        std::string addr_str = EndpointToString(peer_endpoint);

        if (on_connect) {
            on_connect(connection_id, addr_str.c_str());
        }
    };
    auto fn_timeout = [&](uint64_t connection_id) {
        if (on_timeout) {
            on_timeout(connection_id);
        }
    };
    auto fn_data = [&](const QuicheMailbox::Event& event) {
        route_data(event, on_request, on_response);
    };

    server->Poll(fn_connect, fn_timeout, fn_data, timeout_msec);
    return 1;
}

void quicsend_server_respond(
    QuicSendServer* server,
    uint64_t connection_id,
    int64_t request_id,
    int32_t status,
    const PythonBody* body)
{
    if (server == NULL) {
        return;
    }
    BodyData bd = PythonBodyToBodyData(body);
    server->Respond(connection_id, request_id, status, bd);
}

void quicsend_server_close(
    QuicSendServer* server,
    uint64_t connection_id)
{
    if (server == NULL) {
        return;
    }
    server->Close(connection_id);
}


} // extern "C"
