import ctypes
import os
import site
from enum import Enum
from typing import Any, Optional
import msgpack

# Load the shared library (assuming it's named 'quicsend_library.so')
so_file_name = "quicsend_library.so"
lib_path = None
for path in site.getsitepackages():
    potential_path = os.path.join(path, so_file_name)
    if os.path.exists(potential_path):
        lib_path = potential_path
        break

if not lib_path:
    raise FileNotFoundError(f"Shared library not found. Please ensure {so_file_name} is installed correctly.")

lib = ctypes.CDLL(lib_path)

# These must be kept in sync with quicsend_python.h
class Body(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("ContentType", ctypes.c_char_p),
        ("Data", ctypes.py_object),
        ("Length", ctypes.c_int32),
    ]

class Request(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("ConnectionAssignedId", ctypes.c_uint64),
        ("RequestId", ctypes.c_int64),
        ("Path", ctypes.c_char_p),
        ("HeaderInfo", ctypes.c_char_p),
        ("Body", Body),
    ]

class Response(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("ConnectionAssignedId", ctypes.c_uint64),
        ("RequestId", ctypes.c_int64),
        ("Status", ctypes.c_int32),
        ("HeaderInfo", ctypes.c_char_p),
        ("Body", Body)
    ]

class PythonQuicSendClientSettings(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("AuthToken", ctypes.c_char_p),
        ("Host", ctypes.c_char_p),
        ("CertPath", ctypes.c_char_p),
        ("Port", ctypes.c_uint16),
    ]

class PythonQuicSendServerSettings(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("AuthToken", ctypes.c_char_p),
        ("CertPath", ctypes.c_char_p),
        ("KeyPath", ctypes.c_char_p),
        ("Port", ctypes.c_uint16),
    ]

# Define callback function types
CONNECT_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_uint64, ctypes.c_char_p)
TIMEOUT_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_uint64)
REQUEST_CALLBACK = ctypes.CFUNCTYPE(None, Request)
RESPONSE_CALLBACK = ctypes.CFUNCTYPE(None, Response)

# Set up function signatures
lib.quicsend_client_create.argtypes = [ctypes.POINTER(PythonQuicSendClientSettings)]
lib.quicsend_client_create.restype = ctypes.c_void_p

lib.quicsend_client_destroy.argtypes = [ctypes.c_void_p]
lib.quicsend_client_destroy.restype = None

lib.quicsend_client_request.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, Body]
lib.quicsend_client_request.restype = ctypes.c_int64

lib.quicsend_client_poll.argtypes = [ctypes.c_void_p, CONNECT_CALLBACK, TIMEOUT_CALLBACK, RESPONSE_CALLBACK, ctypes.c_int32]
lib.quicsend_client_poll.restype = ctypes.c_int32

lib.quicsend_server_create.argtypes = [ctypes.POINTER(PythonQuicSendServerSettings)]
lib.quicsend_server_create.restype = ctypes.c_void_p

lib.quicsend_server_destroy.argtypes = [ctypes.c_void_p]
lib.quicsend_server_destroy.restype = None

lib.quicsend_server_poll.argtypes = [ctypes.c_void_p, CONNECT_CALLBACK, TIMEOUT_CALLBACK, REQUEST_CALLBACK, ctypes.c_int32]
lib.quicsend_server_poll.restype = ctypes.c_int32

lib.quicsend_server_respond.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int64, ctypes.c_int32, ctypes.c_char_p, Body]
lib.quicsend_server_respond.restype = None

lib.quicsend_server_close.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
lib.quicsend_server_close.restype = None

def ToBody(data: Any) -> Body:
    body = Body()
    if isinstance(data, bytes):
        body.ContentType = b"application/octet-stream"
        body.datab_ = data
    elif isinstance(data, str):
        body.ContentType = b"text/plain"
        body.datab_ = data.encode()
    elif isinstance(data, dict):
        body.ContentType = b"application/msgpack"
        body.datab_ = msgpack.packb(data, use_bin_type=False) 
    else:
        raise TypeError("ToBody: Unexpected data type")
    body.Data = body.datab_
    body.Length = len(body.datab_)
    return body

def FromBody(body: Body) -> Any:
    data = body.Data

    if data is None or body.Length <= 0:
        return None

    if body.ContentType == b"application/msgpack":
        return msgpack.unpackb(data.tobytes(), raw=False)
    elif body.ContentType == b"application/octet-stream":
        return data # MemoryView object for zero-copy
    elif body.ContentType == b"text/plain":
        return data.tobytes().decode()
    else:
        raise TypeError("FromBody:Unexpected content type")

class Client:
    def __init__(self,
                 auth_token: str,
                 host: str,
                 port: int,
                 cert_path: str):
        settings = PythonQuicSendClientSettings(
            AuthToken=auth_token.encode(),
            Host=host.encode(),
            Port=port,
            CertPath=cert_path.encode()
        )
        self.client = lib.quicsend_client_create(ctypes.byref(settings))
        if not self.client:
            raise RuntimeError("Failed to create QuicSend client")

    def __del__(self):
        self.destroy()

    def destroy(self):
        if self.client:
            lib.quicsend_client_destroy(self.client)
            self.client = None

    def request(self,
                path: str,
                header_info: Optional[str] = None,
                body: Optional[Body] = Body()) -> int:
        path_encoded = path.encode()
        header_info_encoded = header_info.encode() if header_info else None

        return lib.quicsend_client_request(
            self.client,
            path_encoded,
            header_info_encoded,
            body)

    def poll(self, on_connect, on_timeout, on_response, timeout_msec):
        def connect_callback(connection_id, peer_endpoint):
            on_connect(connection_id, peer_endpoint.decode())

        def timeout_callback(connection_id):
            on_timeout(connection_id)

        def response_callback(response):
            on_response(response)

        connect_cb = CONNECT_CALLBACK(connect_callback)
        timeout_cb = TIMEOUT_CALLBACK(timeout_callback)
        response_cb = RESPONSE_CALLBACK(response_callback)

        return lib.quicsend_client_poll(self.client, connect_cb, timeout_cb, response_cb, timeout_msec)

class Server:
    def __init__(self,
                 auth_token: str,
                 port: int,
                 cert_path: str,
                 key_path: str):
        settings = PythonQuicSendServerSettings(
            AuthToken=auth_token.encode(),
            Port=port,
            CertPath=cert_path.encode(),
            KeyPath=key_path.encode()
        )
        self.server = lib.quicsend_server_create(ctypes.byref(settings))
        if not self.server:
            raise RuntimeError("Failed to create QuicSend server")

    def __del__(self):
        self.destroy()

    def destroy(self):
        if self.server:
            lib.quicsend_server_destroy(self.server)
            self.server = None

    def poll(self, on_connect, on_timeout, on_request, timeout_msec):
        def connect_callback(connection_id, peer_endpoint):
            on_connect(connection_id, peer_endpoint.decode())

        def timeout_callback(connection_id):
            on_timeout(connection_id)

        def request_callback(request):
            on_request(request)

        connect_cb = CONNECT_CALLBACK(connect_callback)
        timeout_cb = TIMEOUT_CALLBACK(timeout_callback)
        request_cb = REQUEST_CALLBACK(request_callback)

        return lib.quicsend_server_poll(self.server, connect_cb, timeout_cb, request_cb, timeout_msec)

    def respond(self,
                connection_id: int,
                request_id: int,
                status: int,
                header_info: Optional[str] = None,
                body: Optional[Body] = Body()):
        header_info_encoded = header_info.encode() if header_info else None

        lib.quicsend_server_respond(
            self.server,
            connection_id,
            request_id,
            status,
            header_info_encoded,
            body)

    def close(self, connection_id):
        lib.quicsend_server_close(self.server, connection_id)
