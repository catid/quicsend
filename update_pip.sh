#!/bin/bash

# Clean build artifacts
rm -rf build/
rm -rf dist/

docker run --rm -v `pwd`:/io quay.io/pypa/manylinux2014_x86_64 /io/build-wheels.sh

# Check package before uploading (optional but recommended)
twine check dist/*

# Upload to PyPI (use TestPyPI first for testing)
twine upload dist/*
