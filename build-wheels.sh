#!/bin/bash

# This script is run inside a Docker container to build the wheels:
# https://github.com/pypa/manylinux

# Debug:
# docker run --rm -it -v "$(pwd)":/io quay.io/pypa/manylinux2014_x86_64 /bin/bash

set -e  # Exit immediately if any command fails

# Detect architecture
ARCH=$(uname -m)

# Set PLATFORM for auditwheel based on architecture
if [ "$ARCH" == "x86_64" ]; then
    PLATFORM="manylinux_2_28_x86_64"
elif [ "$ARCH" == "aarch64" ]; then
    PLATFORM="manylinux_2_28_aarch64"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

echo "Building wheels for architecture: $ARCH"
echo "Using platform tag: $PLATFORM"

mkdir -p /io/deps
cd /io/deps

# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source $HOME/.cargo/env  # Add Cargo to PATH

# Update Boost Version in the Docker Container
yum install -y wget bzip2

# Download Boost (replace with the desired version)
BOOST_VERSION="1.78.0"
wget https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION//./_}.tar.bz2

# Extract
tar --bzip2 -xf boost_${BOOST_VERSION//./_}.tar.bz2

# Build and Install
cd boost_${BOOST_VERSION//./_}
./bootstrap.sh
./b2 install link=static threading=multi

cd /io

rm -rf /io/deps

# Install Python build tools and OpenSSL
yum install -y python3-pip openssl-devel python3-devel

# Clean build artifacts
rm -rf dist/

# Upgrade build tools
python3 -m pip install --upgrade build wheel auditwheel msgpack

# Build the wheel
rm -rf build/
python3.6 -m build --wheel

rm -rf build/
python3.7 -m build --wheel

rm -rf build/
python3.8 -m build --wheel

rm -rf build/
python3.9 -m build --wheel

rm -rf build/
python3.10 -m build --wheel

mkdir -p /io/repaired

for whl in dist/*.whl; do
    auditwheel repair "$whl" --plat "$PLATFORM" -w /io/repaired/
done

rm -rf build/
rm -rf dist/
