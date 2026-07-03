# Installation

PolyEnv is distributed as a Python package with a C++ extension module. Installing it builds the engine bindings for your local Python.

## Install From GitHub

```bash
pip install git+https://github.com/FryOne2137/PolyEnv.git
```

The package declares `numpy` as a runtime dependency, so users do not need to install it separately.

## Requirements

- Python `>=3.10`
- A C++20-capable compiler
- CMake `>=3.20`
- `pip`

Build dependencies such as `scikit-build-core` and `pybind11` are declared in `pyproject.toml` and are installed automatically by modern `pip`.

## Install From A Local Checkout

```bash
git clone https://github.com/FryOne2137/PolyEnv.git
cd PolyEnv
pip install .
```

For development:

```bash
pip install -e .
```

## Quick Check

```python
from game_engine import GameEnv, tribes

env = GameEnv(seed=1234, map_size=11, players=(tribes.Bardur, tribes.Imperius))
packet = env.model_request_numpy()

print(packet["map_tokens"].shape)
print(packet["actions"]["action_id"])
```

Expected shape for an 11x11 map:

```text
(121, 18)
```

## Local Documentation

```bash
pip install -r docs/requirements.txt
mkdocs serve
```

Open:

```text
http://127.0.0.1:8000
```

## Common Install Command For Users

For most users, this is enough:

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
```

Use `python -m pip` instead of bare `pip` when you have multiple Python versions installed.
