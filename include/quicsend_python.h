#pragma once

#include <quicsend_client.hpp>
#include <quicsend_server.hpp>

extern "C" {


//------------------------------------------------------------------------------
// C API

struct PythonRequestData {
    uint64_t ConnectionAssignedId;
    int32_t b_IsResponse; // Non-zero if this is a response
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

struct PythonQuicSendClientSettings {
    const char* Host;
    uint16_t Port;
    const char* CertPath;
};

QuicSendClient* quicsend_client_create(const PythonQuicSendClientSettings* settings);

void quicsend_client_destroy(QuicSendClient *client);

int64_t quicsend_client_request(
    QuicSendClient *client,
    const char* path,
    const char* content_type, // Optional
    const void* data, // Optional
    int32_t bytes); // Optional

// Returns non-zero if the client is still valid
int32_t quicsend_client_poll(QuicSendClient *client,
                          connect_callback on_connect,
                          timeout_callback on_timeout,
                          data_callback on_data,
                          int32_t timeout_msec);


//------------------------------------------------------------------------------
// C API : QuicSendServer

struct PythonQuicSendServerSettings {
    uint16_t Port;
    const char* CertPath;
    const char* KeyPath;
};

QuicSendServer* quicsend_server_create(const PythonQuicSendServerSettings* settings);

void quicsend_server_destroy(QuicSendServer *server);

int64_t quicsend_server_request(
    QuicSendServer *server,
    uint64_t connection_id,
    const char* path,
    const char* content_type,
    const void* data,
    int32_t bytes);

// Returns non-zero if the server is still valid
int32_t quicsend_server_poll(QuicSendServer *server,
                          connect_callback on_connect,
                          timeout_callback on_timeout,
                          data_callback on_data,
                          int32_t timeout_msec);


} // extern "C"
