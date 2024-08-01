#!/bin/bash
pip install build

rm -rf build dist quicsend.egg-info && pip uninstall quicsend -y
python -m build && pip install --force-reinstall dist/*.whl
