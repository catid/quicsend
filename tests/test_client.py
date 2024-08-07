import signal
import sys
import time
from quicsend import Client, Response, ToBody, FromBody
import ctypes

# Global variables
client = None
t0 = 0
terminated = False

def signal_handler(signum, frame):
    global terminated
    print(f"Interrupt signal ({signum}) received.")
    terminated = True

def get_nsec():
    return int(time.time() * 1e9)

def dump_hex(ptr, size, label=None):
    # Ensure we don't try to print more than what's available
    dump_size = min(size, 32)
    
    # Create a bytes object from the memory pointed to by the void_p
    data = ctypes.string_at(ptr, dump_size)
    
    # Convert to hex
    hex_dump = ' '.join([f'{b:02x}' for b in data])
    
    # Prepare the output string
    output = f"{label + ' ' if label else ''}({size} bytes): {hex_dump}"
    
    # Add ellipsis if there's more data
    if size > 32:
        output += " ..."
    
    print(output)

def on_connect(connection_id: int, peer_endpoint: str):
    global t0
    print(f"OnConnect: cid={connection_id} addr={peer_endpoint}")
    
    t0 = get_nsec()

    rid = client.request("simple.txt", header_info='{"foo": "bar"}')
    print(f"Send request id={rid}")

def on_timeout(connection_id: int):
    print(f"OnTimeout: cid={connection_id}")

def on_response(response: Response):
    global t0
    t1 = get_nsec()
    throughput = response.Body.Length * 1000.0 / (t1 - t0)
    print(f"Throughput: {throughput:.2f} MB/s")

    print(f"OnResponse: cid={response.ConnectionAssignedId} rid={response.RequestId} "
          f"hinfo={response.HeaderInfo.decode() if response.HeaderInfo else None} status={response.Status} "
          f"ct={response.Body.ContentType.decode() if response.Body.ContentType else None} len={response.Body.Length}")

    # Call our function
    dump_hex(response.Body.Data, 32, "Sample")

    t0 = get_nsec()

    rid = client.request("simple.txt", header_info='{"foo": "bar"}')
    print(f"Send request id={rid}")

def main():
    global client

    host = sys.argv[1] if len(sys.argv) >= 2 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) >= 3 else 4433
    cert_path = sys.argv[3] if len(sys.argv) >= 4 else "server.pem"

    signal.signal(signal.SIGINT, signal_handler)

    try:
        client = Client("AUTH_TOKEN_PLACEHOLDER", host, port, cert_path)

        while not terminated:
            result = client.poll(on_connect, on_timeout, on_response, 100)
            if result == 0:
                break

    except Exception as e:
        print(f"Exception: {str(e)}")
        return -1
    finally:
        if client:
            client.destroy()

    return 0

if __name__ == "__main__":
    sys.exit(main())
