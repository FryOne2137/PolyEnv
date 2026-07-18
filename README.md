# PolyEnv

PolyEnv is an unofficial Polytopia-like game engine for Python. It provides a
fast C++ simulation core for AI training, external bots, MCTS, and replay
viewing.

It supports the 12 regular tribes. It is not affiliated with The Battle of
Polytopia, does not implement special tribes, and does not include a trained
bot or reward shaping.

For high-throughput RL training, `VectorGameEnv` runs many independent games
in a native C++ worker pool and returns dense batched NumPy arrays without a
Python loop or JSON serialization per game.

## Install

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
```

Uninstall:

```bash
python -m pip uninstall PolyEnv
```

`numpy` is installed automatically. Building from source requires Python 3.10+
a C++20 compiler and CMake 3.20+.

## Use

```python
from PolyEnv import GameEnv, Bardur, Imperius, Lakes

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius), map_type=Lakes)

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])
ok, done, reward, winner, current_player = env.step_fast(action_id)
```

`model_request_numpy()` returns the current player's visible map, game/player
state, and legal actions. Choose only an `action_id` from
`packet["actions"]["action_id"]`.

## Batched Training

```python
from PolyEnv import VectorGameEnv

env = VectorGameEnv(num_envs=256, num_threads=8, max_actions=512)
batch = env.reset()
action_ids = batch["action_id"][:, 0]
batch = env.step(action_ids)
```

`VectorGameEnv` uses fixed padded action rows; apply `batch["action_mask"]`
before selecting a row. See [native batched training](docs/vector_env.md) for
the complete tensor layout and rollout pattern.

For an allocation-free GPU rollout path, the same API can fill caller-owned
NumPy buffers in place through `batch_spec()`, `reset_into()` and `step_into()`.
Those buffers may be views of pinned PyTorch CPU tensors; CUDA streams and
events remain owned by the external training repository. The vector guide
shows the safe double-buffering protocol. For asynchronous actions, preserve
the returned `state_id` and use `step_checked()` / `step_into_checked()`;
`slot_partitions(2..4)` plus the slot APIs provide disjoint live-game groups
for a safe CPU/GPU pipeline.

Set `visible_event_history=K` when the policy needs the last `K` events that
are visible under fog of war. The vector API returns these as fixed-size masked
arrays; leaving the default `K=0` keeps that encoding disabled.

For batched neural MCTS, `MctsPool` keeps PUCT trees and game branches in C++
and exchanges only one dense leaf batch per GPU inference round. See
[native batched MCTS](docs/mcts_pool.md). It supports `1..8` pending leaves
per tree with native virtual loss, cancellation of abandoned batches, and hard
per-tree node/byte admission budgets.

`MctsPool` and `SelfPlayPool` also provide fixed-capacity reusable leaf/root
buffers (`*_batch_spec()` and `*_into()`); this is the pinned-memory path for
external GPU trainers, including dynamic leaf batches via a returned prefix
length.

For external AI projects that need many live games plus fog-of-war
belief-rooted MCTS, `SelfPlayPool` keeps the entire simulation/search
scheduler in C++ and exchanges only dense NumPy batches with the external
model. It has no PyTorch or CUDA dependency; see
[native belief-MCTS self-play](docs/self_play_pool.md).

`map_type` accepts `Lakes` / `"lakes"` or `Drylands` / `"drylands"`.

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
- [Native batched training](docs/vector_env.md)
- [Native batched MCTS](docs/mcts_pool.md)
- [Native belief-MCTS self-play](docs/self_play_pool.md)
- [Model input and legal actions](docs/model_request_api.md)
- [GUI build and replay viewer](docs/gui.md)

## Attribution

Map generation is based on
[QuasiStellar/Polytopia-Map-Generator](https://github.com/QuasiStellar/Polytopia-Map-Generator)
and was modified for PolyEnv.
