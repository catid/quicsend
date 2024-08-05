#pragma once

#include <quicsend_client.hpp>
#include <quicsend_server.hpp>

extern "C" {


//------------------------------------------------------------------------------
// C API

struct PythonBody {
    const char* ContentType;
    const void* Data;
    int32_t Length;
};

struct PythonRequest {
    uint64_t ConnectionAssignedId;
    int64_t RequestId;

    const char* Path;
    const char* HeaderInfo;
    PythonBody Body;
};

struct PythonResponse {
    uint64_t ConnectionAssignedId;
    int64_t RequestId;

    int32_t Status;
    const char* HeaderInfo;
    PythonBody Body;
};

typedef void (*connect_callback)(uint64_t connection_id, const char* peer_endpoint);
typedef void (*timeout_callback)(uint64_t connection_id);
typedef void (*request_callback)(PythonRequest request);
typedef void (*response_callback)(PythonResponse response);


//------------------------------------------------------------------------------
// C API : QuicSendClient

struct PythonQuicSendClientSettings {
    const char* AuthToken;
    const char* Host;
    uint16_t Port;
    const char* CertPath;
};

QuicSendClient* quicsend_client_create(const PythonQuicSendClientSettings* settings);

void quicsend_client_destroy(QuicSendClient* client);

int64_t quicsend_client_request(
    QuicSendClient* client,
    const char* path,
    const char* header_info, // Optional string sent in headers
    const PythonBody* body); // Optional

// Returns non-zero if the client is still valid
int32_t quicsend_client_poll(
    QuicSendClient* client,
    connect_callback on_connect,
    timeout_callback on_timeout,
    response_callback on_response,
    int32_t timeout_msec);


//------------------------------------------------------------------------------
// C API : QuicSendServer

struct PythonQuicSendServerSettings {
    const char* AuthToken;
    uint16_t Port;
    const char* CertPath;
    const char* KeyPath;
};

QuicSendServer* quicsend_server_create(const PythonQuicSendServerSettings* settings);

void quicsend_server_destroy(QuicSendServer* server);

// Returns non-zero if the server is still valid
int32_t quicsend_server_poll(
    QuicSendServer* server,
    connect_callback on_connect,
    timeout_callback on_timeout,
    request_callback on_request,
    int32_t timeout_msec);

void quicsend_server_respond(
    QuicSendServer* server,
    uint64_t connection_id,
    int64_t request_id,
    int32_t status,
    const char* header_info, // Optional string sent in headers
    const PythonBody* body);

void quicsend_server_close(
    QuicSendServer* server,
    uint64_t connection_id);


} // extern "C"
