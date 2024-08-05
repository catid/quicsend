import signal
import sys
from quicsend import Server, PythonBody, PythonRequest

# Global variables
server = None
terminated = False

def signal_handler(signum, frame):
    global terminated
    print(f"Interrupt signal ({signum}) received.")
    terminated = True

def on_connect(connection_id, peer_endpoint):
    print(f"OnConnect: cid={connection_id} addr={peer_endpoint}")

def on_timeout(connection_id):
    print(f"OnTimeout: cid={connection_id}")

def on_request(request):
    print(f"OnRequest: cid={request.ConnectionAssignedId} rid={request.RequestId} "
          f"hinfo={request.HeaderInfo.decode() if request.HeaderInfo else None} "
          f"path={request.Path.decode()} "
          f"ct={request.Body.ContentType.decode() if request.Body.ContentType else None} "
          f"len={request.Body.Length}")

    # Create a large response
    response = b'A' * (512 * 1024 * 1024)  # 512 MB of 'A' characters

    body = PythonBody()
    body.ContentType = b"text/plain"
    body.Data = response
    body.Length = len(response)

    server.respond(request.ConnectionAssignedId, request.RequestId, 200, 
                   header_info=request.HeaderInfo, body=body)

def main():
    global server

    port = int(sys.argv[1]) if len(sys.argv) >= 2 else 4433
    cert_path = sys.argv[2] if len(sys.argv) >= 3 else "server.pem"
    key_path = sys.argv[3] if len(sys.argv) >= 4 else "server.key"

    signal.signal(signal.SIGINT, signal_handler)

    try:
        server = Server("AUTH_TOKEN_PLACEHOLDER", port, cert_path, key_path)

        while not terminated:
            result = server.poll(on_connect, on_timeout, on_request, 100)
            if result == 0:
                break

    except Exception as e:
        print(f"Exception: {str(e)}")
        return 1
    finally:
        if server:
            server.destroy()

    return 0

if __name__ == "__main__":
    sys.exit(main())
