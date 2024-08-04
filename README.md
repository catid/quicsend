# quicsend :: High-performance Internet-ready RPC/file transfers from Python

Use Cloudflare quiche to host secure HTTP3 RPC/file transfer servers from Python on the open Internet.

This was developed while working towards a distributed model training system: I was looking for a secure way to send model/pseudo-gradients to/from remote docker containers as fast as possible.  Hence, there's certificate pinning for MitM protection, and client authentication to avoid misuse of the server.  It uses multiple parallel connections with BBRv2 congestion contorl to transparently to make full use of the available bandwidth.  It exposes a simple polling interface to Python so that it can be used directly from training scripts for convenience.  The code is all in C++ for performance reasons, with the Internet-exposed server written in Rust and provided by the quiche library.

Note that quiche is fairly non-trivial to use since there are a lot of API calls that each have failure modes during congestion that must be handled appropriately.  It took me about two weeks and lots of unit testing to understand how to use it correctly.  But I've verified that it works correctly and does not leak memory.


# Certificate Management

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
sudo apt install cmake build-essential cargo libboost-system-dev

pip install quicsend
```

Follow the example code in `example_client.py` and `example_server.py` to get started.


## Manual Build Instructions

```bash
git clone https://github.com/catid/quicsend.git
git submodule update --init --recursive

sudo apt install cmake build-essential cargo libboost-system-dev

./install.sh
```


## Ackwnoledgements

This project is based on the Cloudflare quiche library: https://github.com/cloudflare/quiche
