import ctypes
import os
import site
from enum import Enum

class PeerState(Enum):
    CONNECTING = 1
    CONNECTED = 2
    DISCONNECTED = 3

class DataType(Enum):
    TEXT = 1
    BINARY = 2

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

lib.quicsend_client_create.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_char_p]
lib.quicsend_client_create.restype = ctypes.c_void_p

lib.quicsend_client_destroy.argtypes = [ctypes.c_void_p]
lib.quicsend_client_destroy.restype = None

lib.quicsend_client_request.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
lib.quicsend_client_request.restype = None

lib.quicsend_client_poll.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
lib.quicsend_client_poll.restype = None

# Define callback function types
CONNECT_CALLBACK = ctypes.CFUNCTYPE(None)
DISCONNECT_CALLBACK = ctypes.CFUNCTYPE(None)
REQUEST_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_int)
RESPONSE_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_int)

class Client:
    def __init__(self, server_address, port, cert_file, auth_key):
        self.client = lib.quicsend_client_create(server_address.encode(), port, ctypes.c_int, cert_file.encode(), auth_key.encode())
        if not self.client:
            raise RuntimeError("Failed to create quiche client")
        self.responses = {}

    def __del__(self):
        self.destroy()

    def destroy(self):
        if self.client:
            lib.quicsend_client_destroy(self.client)
            self.client = None

    def request(self, endpoint_path, body, response_callback):
        if body == None:
            request_id = self.quicsend_client_request(self.client, endpoint_path, ctypes.c_void_p(None), 0)
        elif isinstance(body, str):
            encoded_body = body.encode('utf-8')
            ptr = ctypes.cast(ctypes.create_string_buffer(encoded_body), ctypes.c_void_p)
            request_id = self.quicsend_client_request(self.client, endpoint_path, ptr, len(encoded_body))
        elif isinstance(body, bytes):
            request_type = DataType.BINARY
        else:
            raise RuntimeError("Unsupported data type")

    # This function will invoke response callbacks that were previously registered as well
    def poll(self, connect_callback, disconnect_callback, request_callback):
        self.connect_callback = CONNECT_CALLBACK(connect_callback)
        self.disconnect_callback = DISCONNECT_CALLBACK(disconnect_callback)
        self.request_callback = REQUEST_CALLBACK(request_callback)
        self.response_callback = RESPONSE_CALLBACK(response_callback)

        lib.quicsend_client_poll(self.client, 
                                 self.connect_callback,
                                 self.disconnect_callback,
                                 self.request_callback,
                                 self.response_callback)
