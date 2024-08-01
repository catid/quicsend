# Distributed C++ Extensions

Uses `quiche` for HTTP3 transport security and BBRv2 congestion control.  Saturates 10G Internet connections with a single server.


# Setup

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

To uninstall the certificate:

```bash
sudo rm /usr/local/share/ca-certificates/root_quicsend.crt

sudo update-ca-certificates --fresh
```


## Build

```bash
git clone https://github.com/catid/quicsend.git
git submodule update --init --recursive

sudo apt install cmake build-essential cargo libboost-system-dev

./install.sh
```
