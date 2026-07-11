# PolyEnv

PolyEnv is an unofficial Polytopia-like game engine for Python. It provides a
fast C++ simulation core for AI training, external bots, MCTS, and replay
viewing.

It supports the 12 regular tribes. It is not affiliated with The Battle of
Polytopia, does not implement special tribes, and does not include a trained
bot or reward shaping.

## Install

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
```

`numpy` is installed automatically. Building from source requires Python 3.10+
a C++20 compiler and CMake 3.20+.

## Use

```python
from PolyEnv import GameEnv, Bardur, Imperius

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])
ok, done, reward, winner, current_player = env.step_fast(action_id)
```

`model_request_numpy()` returns the current player's visible map, game/player
state, and legal actions. Choose only an `action_id` from
`packet["actions"]["action_id"]`.

```python
# Explicit ground truth, intended for labels or debugging.
full_map = env.full_map_numpy()

# Independent branch for MCTS.
branch = env.clone()

# Portable replay of the current match.
env.save("match.polygame")
```

## Documentation

- [Documentation](https://polyenv.readthedocs.io/)
- [Installation](docs/installation.md)
- [Core Python API](docs/python_api.md)
- [Model input and legal actions](docs/model_request_api.md)
- [GUI build and replay viewer](docs/gui.md)

## Attribution

Map generation is based on
[QuasiStellar/Polytopia-Map-Generator](https://github.com/QuasiStellar/Polytopia-Map-Generator)
and was modified for PolyEnv.
