#include <quicsend_python.h>


//------------------------------------------------------------------------------
// Tools

PyObject* create_python_view_object(void* data, Py_ssize_t len) {
    if (!data || len <= 0) {
        return Py_None;
    }

    return PyMemoryView_FromMemory((char*)data, len, PyBUF_WRITE);
}

static void route_event(
    const QuicheMailbox::Event& event,
    connect_callback on_connect,
    timeout_callback on_timeout,
    request_callback on_request,
    response_callback on_response)
{
    if (event.Type == QuicheMailbox::EventType::Connect) {
        std::string addr_str = EndpointToString(event.PeerEndpoint);
        on_connect(event.ConnectionAssignedId, addr_str.c_str());
    } else if (event.Type == QuicheMailbox::EventType::Timeout) {
        on_timeout(event.ConnectionAssignedId);
    } else if (event.Type == QuicheMailbox::EventType::Data) {
        if (on_request) {
            PythonRequest request;
            request.ConnectionAssignedId = event.ConnectionAssignedId;
            request.RequestId = event.Stream->Id;

            PyGILState_STATE gstate = PyGILState_Ensure();
            PyObject* py_obj = create_python_view_object(event.Stream->Buffer.data(), event.Stream->Buffer.size());

            request.Path = event.Stream->Path.c_str();
            request.Body.ContentType = event.Stream->ContentType.c_str();
            request.HeaderInfo = event.Stream->HeaderInfo.c_str();
            request.Body.Data = py_obj;
            request.Body.Length = event.Stream->Buffer.size();

            on_request(request);

            if (py_obj) {
                Py_DECREF(py_obj);
            }
            PyGILState_Release(gstate);
        } else if (on_response) {
            PythonResponse response;
            response.ConnectionAssignedId = event.ConnectionAssignedId;
            response.RequestId = event.Stream->Id;

            PyGILState_STATE gstate = PyGILState_Ensure();
            PyObject* py_obj = create_python_view_object(event.Stream->Buffer.data(), event.Stream->Buffer.size());

            response.Status = std::atoi(event.Stream->Status.c_str());
            response.Body.ContentType = event.Stream->ContentType.c_str();
            response.HeaderInfo = event.Stream->HeaderInfo.c_str();
            response.Body.Data = py_obj;
            response.Body.Length = event.Stream->Buffer.size();

            on_response(response);

            if (py_obj) {
                Py_DECREF(py_obj);
            }
            PyGILState_Release(gstate);
        }
    }
}

extern "C" {


//------------------------------------------------------------------------------
// C API : QuicSendClient

QuicSendClient* quicsend_client_create(const PythonQuicSendClientSettings* settings)
{
    QuicSendClientSettings cs;
    cs.Authorization = std::string("Bearer ") + (settings->AuthToken ? settings->AuthToken : "");
    cs.Host = settings->Host ? settings->Host : "";
    cs.Port = settings->Port;
    cs.CertPath = settings->CertPath ? settings->CertPath : "";

    if (cs.Host.empty() || cs.Port == 0 || cs.CertPath.empty()) {
        LOG_ERROR() << "quicsend_client_create: Invalid input";
        return nullptr;
    }

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
    const char* header_info,
    PythonBody body)
{
    if (client == NULL) {
        return -1;
    }

    // Convert Python body to C++ body
    BodyData bd;
    Py_buffer view{};
    if (body.Data && PyObject_GetBuffer(body.Data, &view, PyBUF_SIMPLE) == 0) {
        bd.ContentType = body.ContentType ? body.ContentType : "";
        bd.Data = static_cast<const uint8_t*>(view.buf);
        bd.Length = static_cast<int32_t>(view.len);
    }
    CallbackScope cbs([&]() {
        if (bd.Data) {
            PyBuffer_Release(&view);
        }
    });

    return client->Request(
        path ? path : "",
        header_info ? header_info : "",
        bd);
}

int32_t quicsend_client_poll(
    QuicSendClient *client,
    connect_callback on_connect,
    timeout_callback on_timeout,
    response_callback on_response,
    int32_t timeout_msec)
{
    if (client == NULL || !client->IsRunning()) {
        return 0;
    }

    auto fn_event = [&](const QuicheMailbox::Event& event) {
        route_event(event, on_connect, on_timeout, nullptr, on_response);
    };

    client->mailbox_.Poll(fn_event, timeout_msec);
    return 1;
}


//------------------------------------------------------------------------------
// C API : QuicSendServer

QuicSendServer* quicsend_server_create(const PythonQuicSendServerSettings* settings)
{
    QuicSendServerSettings ss;
    ss.Authorization = std::string("Bearer ") + (settings->AuthToken ? settings->AuthToken : "");
    ss.Port = settings->Port;
    ss.KeyPath = settings->KeyPath ? settings->KeyPath : "";
    ss.CertPath = settings->CertPath ? settings->CertPath : "";

    if (ss.Port == 0 || ss.KeyPath.empty() || ss.CertPath.empty()) {
        LOG_ERROR() << "quicsend_server_create: Invalid input";
        return nullptr;
    }

    return new QuicSendServer(ss);
}

void quicsend_server_destroy(QuicSendServer *server) {
    if (server != NULL) {
        delete server;
    }
}

int32_t quicsend_server_poll(
    QuicSendServer *server,
    connect_callback on_connect,
    timeout_callback on_timeout,
    request_callback on_request,
    int32_t timeout_msec)
{
    if (server == NULL || !server->IsRunning()) {
        return 0;
    }

    auto fn_event = [&](const QuicheMailbox::Event& event) {
        route_event(event, on_connect, on_timeout, on_request, nullptr);
    };

    server->Poll(fn_event, timeout_msec);
    return 1;
}

void quicsend_server_respond(
    QuicSendServer* server,
    uint64_t connection_id,
    int64_t request_id,
    int32_t status,
    const char* header_info,
    PythonBody body)
{
    if (server == NULL) {
        return;
    }

    // Convert Python body to C++ body
    BodyData bd;
    Py_buffer view{};
    if (body.Data && PyObject_GetBuffer(body.Data, &view, PyBUF_SIMPLE) == 0) {
        bd.ContentType = body.ContentType ? body.ContentType : "";
        bd.Data = static_cast<const uint8_t*>(view.buf);
        bd.Length = static_cast<int32_t>(view.len);
    }
    CallbackScope cbs([&]() {
        if (bd.Data) {
            PyBuffer_Release(&view);
        }
    });

    server->Respond(
        connection_id,
        request_id,
        status,
        header_info ? header_info : "",
        bd);
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
