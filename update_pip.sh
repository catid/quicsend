#!/bin/bash

# Clean up repaired wheels
rm -rf repaired/

# Register QEMU interpreters for ARM emulation
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

# Array of architectures to build for
architectures=("x86_64" "aarch64")

for arch in "${architectures[@]}"; do
    echo "Building wheels for architecture: $arch"

    # Run the Docker container for the specified architecture
    docker run --rm -v "$(pwd)":/io quay.io/pypa/manylinux_2_28_$arch /io/build-wheels.sh
done

# Install necessary Python packages
pip install twine auditwheel

# Check and upload all repaired wheels
twine check repaired/*
twine upload repaired/*
