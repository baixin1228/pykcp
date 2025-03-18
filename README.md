## A Python wrapper library for the KCP protocol

### Usage
#### ubuntu:
``` bash
./requirements.sh
meson build
```
``` python
import ikcp
# For more usage, refer to: test.
udp_kcp = ikcp.PyKcp("0.0.0.0", 0)
```
``` bash
# export PYTHONPATH=/to/the/path/of/ikcp.xxxx-xxxx-xxx-xxxx.so
export PYTHONPATH=$PWD/build
python3 main.py
```


#### window:
Todo

#### Mac OS:
Todo