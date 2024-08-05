#!/bin/bash
pip install --upgrade setuptools
pip install --quiet build wheel

rm -rf build dist quicsend.egg-info
pip uninstall quicsend -y

python -m build --wheel --no-isolation

pip install .
