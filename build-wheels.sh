#!/bin/bash

# This script is run inside a Docker container to build the wheels:
# https://github.com/pypa/manylinux
# docker run --rm -v "$(pwd)":/io quay.io/pypa/manylinux2014_x86_64 /io/build-wheels.sh

cd /io

export CIBW_ENVIRONMENT="CIBW_PLATFORM=linux_x86_64"

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

# Clean up
cd ..
rm -f boost_${BOOST_VERSION//./_}.tar.bz2

# Update package lists and install pip
yum install -y python3-pip
yum install -y openssl-devel

# Install the build package.
python3 -m pip install build

# Now proceed with your build commands
python3 -m build --wheel
