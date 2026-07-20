# PolyEnv

PolyEnv is an unofficial Polytopia-like game engine for Python. It provides a
fast C++ simulation core for AI training, external bots, MCTS, and replay
viewing.

It supports the 12 regular tribes. It is not affiliated with The Battle of
Polytopia, does not implement special tribes, and does not include a trained
bot or reward shaping.

For high-throughput RL self-play with batched MCTS, use `SelfPlayPool`. It
keeps live games, ISMCTS trees, and the native C++ worker pool inside the
engine, exchanging only dense batched NumPy arrays with the model. Use
`VectorGameEnv` for flat vectorized rollouts that do not require MCTS.

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

### Windows diagnostic build (native C++ crashes)

For a native crash report with C++ function names and source lines, build from
the repository with MSVC's `RelWithDebInfo` configuration. This installs the
matching `_game_engine.pdb` beside `_game_engine.pyd` and enables the bundled
`cpptrace` diagnostics:

```powershell
git clone https://github.com/FryOne2137/PolyEnv.git
cd PolyEnv
python -m pip install --no-build-isolation --no-deps --force-reinstall -v `
  -Ccmake.build-type=RelWithDebInfo `
  -Ccmake.args=-DGAME_ENGINE_INSTALL_PDB=ON `
  .
```

Verify that the PDB was installed:

```powershell
python -c "import PolyEnv, pathlib; p=pathlib.Path(PolyEnv.__file__).parent; print(p); print(list(p.glob('*.pdb')))"
```

Keep `_game_engine.pyd` and its PDB from the same installation together. On a
native worker exception PolyEnv appends a `C++ throw stack` to the Python
error; on `std::terminate` it prints a native stack to stderr. A PDB can show
the source line, but cannot make memory-access violations recoverable.

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

For external AI projects that need many live games plus fog-of-war ISMCTS,
`SelfPlayPool` keeps the simulation/search scheduler in C++ and exchanges only
dense NumPy batches with the external model. Submit `[B, P, K, tiles, 23]`
per-player belief particles to enable re-determinization when the actor
changes (`K=1` is accepted). By default `require_all_actions=True` and
`max_actions=0`, so every engine-generated legal action—including `EndTurn`,
technology, building and unit actions—reaches the model; use a smaller explicit
capacity only when it still covers the legal set. Set
`visible_action_history=256..1024` when the model needs compact fog-safe
history tokens in both belief and MCTS leaf packets. It has no PyTorch or CUDA
dependency; see
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
