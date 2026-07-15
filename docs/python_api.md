# Core Python API

## Create A Game

```python
from PolyEnv import GameEnv, Bardur, Imperius, Lakes

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius), map_type=Lakes)
```

`seed=0` chooses a random seed. Tribe constants, strings, and `get_tribe()`
are accepted:

```python
from PolyEnv import GameEnv, get_tribe

env = GameEnv(players=(get_tribe("Bardur"), "Imperius"))
```

Only the 12 regular tribes are supported.

## High-Throughput Training

Use `VectorGameEnv` when training over many games at once. It executes a
batch of independent C++ games on a persistent worker pool and returns dense
NumPy arrays rather than one Python packet per game:

```python
from PolyEnv import VectorGameEnv

env = VectorGameEnv(num_envs=256, num_threads=8, max_actions=512)
batch = env.reset()
batch = env.step(batch["action_id"][:, 0])
```

See [VectorGameEnv: Native Batched Training](vector_env.md) for its complete
API and tensor layouts. Use `GameEnv` for a single game, debugging, replays,
or MCTS cloning.

## Map Type

The supported map types are `Lakes` and `Drylands`. Pass either the exported
constant or its lower-case string form:

```python
from PolyEnv import Drylands, GameEnv

dry_game = GameEnv(map_type=Drylands)
same_game = GameEnv(map_type="drylands")
```

`reset()` accepts the same `map_type` argument. Every observation contains a
`map_type` field whose value is either `"lakes"` or `"drylands"`.

## Everyday Methods

| Method | Purpose |
| --- | --- |
| `model_request_numpy()` | Fast NumPy packet for a policy/model |
| `model_request()` | Readable Python packet for debugging |
| `step_fast(action_id)` | Execute one legal action efficiently |
| `step(action_id)` | Execute one action with detailed metadata |
| `legal_action_ids_fast()` | Return current legal action ids |
| `decode_action(action_id)` | Inspect an action id while debugging |
| `observation()` | Current player's visible observation |
| `player_map_numpy()` | Current player's visible map |
| `full_map_numpy()` | Complete map for labels/debugging |
| `current_player()` / `is_done()` | Read turn and terminal state |

`step_fast()` returns:

```python
ok, done, reward, winner, current_player = env.step_fast(action_id)
```

The engine returns terminal reward only: win `1.0`, loss `-1.0`, otherwise
`0.0`. Add reward shaping in the training project, not in PolyEnv.

## Reset And Branch

```python
env.reset(seed=4321, players=(Bardur, Imperius), map_type="lakes")

branch = env.clone()       # independent copy for MCTS
branch2 = env.copy()       # alias for clone()

snapshot = env.save_state()
# ... inspect or modify env ...
env.load_state(snapshot)
```

`clone()` and `copy()` do not mutate the original environment.

## Replays

```python
env.save("match.polygame")

replayed = GameEnv()
replayed.load("match.polygame")
```

Replay files are portable action histories. Use `save_state()` and
`load_state()` only for in-memory snapshots. See [Replays](replays.md) for
compatibility rules.

## Advanced Helpers

`legal_param_actions()`, `action_param_spec()`, `step_param()`, and
`step_param_vec()` are lower-level action APIs. The recommended integration
for new model code is `model_request_numpy()` plus `step_fast(action_id)`.

## Hidden-Map Prediction Worlds

For MCTS rollouts under fog of war, use
`env.make_belief_env(completed_map_tokens)`. It constructs a detached world
from the current observation plus a full predicted token map. See
[Hidden-Map Predictions](hidden_map_predictions.md).

## Visible Event History

`visible_events_numpy(since=0)` returns only events observable by the current
player. It uses compact NumPy arrays and a private cursor; no global event id,
hidden source position, hidden unit type, or hidden owner is exported. See
[Visible event history](visible_events_api.md) for the packet layout and event ids.
