# Installation

## Install From GitHub

```bash
pip install git+https://github.com/FryOne2137/PolyEnv.git
```

The package declares `numpy` as a runtime dependency, so users do not need to install it separately.

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

