# quicsend

Python extension using Cloudflare `quiche` to host secure HTTP3 RPC/file transfer servers from Python on the open Internet.  Provides a zero-copy polling API for easy integration into existing Python scripts.  Automatically serializes Python objects using `msgpack`.

Released under the BSD 3-Clause License for use in research and commercial software.


## Certificate Management

On your central server, generate a root certificate.  If you have multiple servers you can reuse the same root certificate.

```bash
# Generate the root certificate
openssl req -new -x509 -batch -nodes -days 10000 -keyout root_quicsend.key -out root_quicsend.crt
```

On your central server, generate a server certificate and sign it with the root certificate.

```bash
# Generate the server self-signed certificate (to be discarded)
# The CN must match QUIC_TLS_CNAME=catid.io in the code
openssl req -new -batch -nodes -sha256 -keyout server.key -out server_self_signed.csr -subj '/CN=catid.io'

# Create server.pem
openssl x509 -req -days 10000 -in server_self_signed.csr -CA root_quicsend.crt -CAkey root_quicsend.key -CAcreateserial -out server.pem

# Check to make sure that the signature works
openssl verify -CAfile root_quicsend.crt server.pem
```

On each client machine that will connect to the server, install the root certificate.

This involves copying *only* the `root_quicsend.crt` and `server.pem` files to the client machine.

```bash
# Install the new root certificate on the worker machine
sudo cp root_quicsend.crt /usr/local/share/ca-certificates/
sudo update-ca-certificates

# Verify that it works
openssl verify -CAfile /etc/ssl/certs/ca-certificates.crt server.pem
```

To uninstall the certificate from the client machine:

```bash
sudo rm /usr/local/share/ca-certificates/root_quicsend.crt

sudo update-ca-certificates --fresh
```


## Usage

Install the package in your Python environment:

```bash
# Requires latest pip
python -m pip install --upgrade pip

pip install quicsend
```

Follow the example code in `tests/test_client.py` and `tests/test_server.py` to get started.


## Manual Build Instructions

```bash
git clone https://github.com/catid/quicsend.git --recursive

sudo apt install cmake build-essential cargo libboost-system-dev

./install.sh
```


## Discussion

This was developed while working towards a distributed model training system: I was looking for a secure way to send model/pseudo-gradients to/from remote docker containers as fast as possible.  Hence, there's certificate pinning for MitM protection, and client authentication to avoid misuse of the server.  The goal is to use multiple parallel connections with BBR congestion control to make full use of the available bandwidth.  It exposes a simple polling interface to Python so that it can be used directly from training scripts for convenience.  The code is all in C++ for performance reasons, with the Internet-exposed server written in Rust and provided by the quiche library.

Note that quiche is fairly non-trivial to use since there are a lot of API calls, and each call has failure modes to handle with retries during congestion.  It took me about two weeks and lots of unit testing to understand how to use it correctly.  But I've verified that all the corner cases work correctly at this point, and that it does not leak memory.

I optimized and tuned the quiche code, which now achieves 2Gbps (250MB/s) per socket.  It seems to be a CPU bottleneck without any clear way to improve it further based on the profiler.  So to max out a 10Gbps connection, it requires about 8 quicsend servers with requests spread across them.  I added a way to specify a custom header along with each request/response to allow for tagging file pieces on each server connection.


## Acknowledgements

This project is based on the Cloudflare quiche library: https://github.com/cloudflare/quiche
