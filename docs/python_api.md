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

## Hidden-Map Prediction Helper

For MCTS rollouts that use fog-of-war predictions, see
[Hidden-Map Predictions](hidden_map_predictions.md). The function described
there is imported from `PolyEnv`, not called as a `GameEnv` method.
