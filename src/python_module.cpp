#include <quicsend_client.hpp>
#include <quicsend_server.hpp>

extern "C" {


//------------------------------------------------------------------------------
// C API

typedef void (*connect_callback)();
typedef void (*disconnect_callback)();
typedef void (*request_callback)(const char* path, const char *data, int length);
typedef void (*response_callback)(const char *data, int length);


//------------------------------------------------------------------------------
// C API : quicsend_client

QuicSendClient* quicsend_client_create(
    const char *server_hostname,
    uint16_t port,
    const char *cert_file)
{
    QuicSendClient *client = new QuicSendClient(server_hostname, port, cert_file);
    if (client == NULL) {
        return NULL;
    }

    return client;
}

void quicsend_client_destroy(quiche_client *client) {
    if (client != NULL) {
        free(client);
    }
}

int32_t quicsend_client_request(
    quiche_client *client,
    const char* path,
    const void* data,
    int bytes)
{
    if (client == NULL) {
        return;
    }

    return client->Request(path, data, bytes);
}

void quicsend_client_poll(quiche_client *client,
                          connect_callback on_connect,
                          disconnect_callback on_disconnect,
                          request_callback on_request,
                          response_callback on_response) {
    if (client == NULL) {
        return;
    }

    // Implement polling logic here
    // This is a simplified example and may need to be adjusted based on your needs

    // Simulating a connection event
    if (on_connect != NULL) {
        on_connect();
    }

    // Main polling loop
    while (1) {
        // Check for incoming data
        // For example:
        // uint8_t buf[MAX_DATAGRAM_SIZE];
        // quiche_recv_info recv_info;
        // ssize_t read = quiche_conn_recv(client->conn, buf, sizeof(buf), &recv_info);

        // Process received data
        // if (read > 0) {
        //     // Determine if it's a request or response and call appropriate callback
        //     if (is_request(buf, read)) {
        //         if (on_request != NULL) {
        //             on_request((const char*)buf, read);
        //         }
        //     } else {
        //         if (on_response != NULL) {
        //             on_response((const char*)buf, read);
        //         }
        //     }
        // }

        // Check for disconnection
        // if (quiche_conn_is_closed(client->conn)) {
        //     if (on_disconnect != NULL) {
        //         on_disconnect();
        //     }
        //     break;
        // }

        // Add appropriate sleep or yield to prevent busy-waiting
    }
}


//------------------------------------------------------------------------------
// C API : quiche_server

// FIXME


} // extern "C"
