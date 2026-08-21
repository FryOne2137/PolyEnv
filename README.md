# PolyEnv

PolyEnv is an unofficial Polytopia-like game engine for Python. It provides a fast C++ simulation core for AI training, external bots, MCTS, and replay viewing.

It supports the 12 regular tribes. It is not affiliated with The Battle of Polytopia, does not implement special tribes, and does not include a trained bot or reward shaping.

[Documentation](https://polyenv.readthedocs.io/) · [Installation guide](docs/installation.md) · [Python API](docs/python_api.md)

## PolyBot

PolyBot is a separate module currently in development. It will use PolyEnv for training a bot that plays Polytopia and will be released when it is complete.

## Installation

```bash
pip install git+https://github.com/FryOne2137/PolyEnv.git
```

## Quick start

```python
from PolyEnv import GameEnv, Bardur, Imperius, Lakes

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius), map_type=Lakes)

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])
ok, done, reward, winner, current_player = env.step_fast(action_id)
```

This is the simplest way to run one environment. PolyEnv also provides faster native interfaces for large AI workloads:

| Interface | Best for |
| --- | --- |
| [`VectorGameEnv`](docs/vector_env.md) | Parallel rollouts across many live games |
| [`MctsPool`](docs/mcts_pool.md) | Batched neural MCTS with trees managed in C++ |
| [`SelfPlayPool`](docs/self_play_pool.md) | High-throughput belief-MCTS self-play with an external model |

The complete API, tensor layouts, legal-action format, replay support, and optimized caller-owned buffer paths are described in the [full documentation](https://polyenv.readthedocs.io/).

## Map generation

Map generation is based on [QuasiStellar/Polytopia-Map-Generator](https://github.com/QuasiStellar/Polytopia-Map-Generator) and was modified for PolyEnv.
