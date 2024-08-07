#!/bin/bash

docker run --rm -v "$(pwd)":/io quay.io/pypa/manylinux_2_28_x86_64 /io/build-wheels.sh

pip install twine auditwheel

twine check repaired/*
twine upload repaired/*
