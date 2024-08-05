# quicsend

Use Cloudflare quiche to host secure HTTP3 RPC/file transfer servers from Python on the open Internet.

Released under the BSD 3-Clause License for use in research and commercial software.

See [README.md](README.md) in the repo: https://github.com/catid/quicsend


## Usage

Install the package in your Python environment:

```bash
pip install quicsend
```

Follow the example code in `tests/test_client.py` and `tests/test_server.py` to get started.


## Manual Build Instructions

```bash
git clone https://github.com/catid/quicsend.git --recursive

sudo apt install cmake build-essential cargo libboost-system-dev

./install.sh
```


## Acknowledgements

This project is based on the Cloudflare quiche library: https://github.com/cloudflare/quiche
