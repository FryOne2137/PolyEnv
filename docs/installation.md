# Installation

## Install From GitHub

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
```

This installs NumPy automatically and builds the C++ extension for the Python
interpreter used by that command.

Requirements: Python 3.10+, `pip`, CMake 3.20+, and a C++20-capable compiler.

## Install A Local Checkout

```bash
git clone https://github.com/FryOne2137/PolyEnv.git
cd PolyEnv
python -m pip install .
```

For development, use `python -m pip install -e .`.

## Check The Install

```python
from PolyEnv import GameEnv, Bardur, Imperius

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))
packet = env.model_request_numpy()

print(packet["map_tokens"].shape)  # (121, 18) for an 11 x 11 map
print(len(packet["actions"]["action_id"]))
```

If `PolyEnv` cannot be imported, verify that `python` and `pip` point to the
same environment:

```bash
python -m pip --version
python -c "import sys; print(sys.executable)"
```

For local documentation, run `python -m pip install -r docs/requirements.txt`
and then `mkdocs serve`.
