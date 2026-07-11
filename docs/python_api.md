# Python API

This page lists the core Python API used by training and inference code.

## Create An Environment

```python
from PolyEnv import GameEnv, Bardur, Imperius

env = GameEnv(
    seed=1234,
    map_size=11,
    players=(Bardur, Imperius),
)
```

You can also pass tribe names through `get_tribe` or as strings:

```python
from PolyEnv import GameEnv, get_tribe

env = GameEnv(
    seed=1234,
    map_size=11,
    players=(get_tribe("Bardur"), get_tribe("Imperius")),
)

env2 = GameEnv(seed=1234, map_size=11, players=("Bardur", "Imperius"))
```

Only the 12 regular tribes are supported by the public API. Special tribes are intentionally rejected for this release because their unique mechanics are not implemented to conformance level.

## State Methods

| Method | Returns | Use |
| --- | --- | --- |
| `env.model_request()` | `dict` with `map_tokens`, `obs`, `actions`, `spec` | Debuggable canonical model packet |
| `env.model_request_numpy()` | `dict` with NumPy arrays for hot-path fields | Training and inference |
| `env.observation()` | `dict` | Current player's visible observation |
| `env.tokenized_map()` | `list[list[int]]` | Current player's visible map tokens |
| `env.player_map()` | `list[list[int]]` | Explicit player-view map |
| `env.player_map_numpy()` | `np.ndarray[int32]` | Explicit player-view map for training |
| `env.full_map()` | `list[list[int]]` | Explicit ground-truth map |
| `env.full_map_numpy()` | `np.ndarray[int32]` | Ground-truth map for targets/debugging |
| `env.current_player()` | `int` | Current player id |
| `env.is_done()` | `bool` | Terminal state check |

By default, model and observation APIs return only what the current player can observe. Use `full_map*` only when you explicitly need ground-truth map labels, for example for a hidden-tile prediction model.

## Map Methods

Default map methods are player-view:

```python
assert env.tokenized_map() == env.player_map()
assert env.model_request()["map_tokens"] == env.player_map()
```

Use explicit methods when the distinction matters:

```python
visible = env.player_map_numpy()
target = env.full_map_numpy()
```

`visible` is suitable for policy/model input. `target` is suitable for hidden-tile prediction labels or debugging.

## Action Methods

| Method | Returns | Use |
| --- | --- | --- |
| `env.legal_param_actions()` | `list[dict]` | Debug legal actions |
| `env.legal_action_ids_fast()` | `list[int]` | Fast legal action ids |
| `env.decode_action(action_id)` | `dict` | Inspect one legal action id |
| `env.action_param_spec()` | `dict` | Vocab sizes and argument order |
| `env.step_fast(action_id)` | tuple | Fast environment step |
| `env.step(action_id)` | `dict` | Step with detailed output |
| `env.save(path)` | `Path` | Write a `.polygame` action replay through the native C++ recorder |
| `env.load(path)` | `dict` | Recreate a replay through the native C++ loader and return its final observation |
| `env.replay_action_ids()` | `list[int]` | Accepted action ids recorded for the current match |

## Step Return Values

`step_fast` returns:

```python
ok, done, reward, winner, current_player = env.step_fast(action_id)
```

| Field | Type | Meaning |
| --- | --- | --- |
| `ok` | `bool` | Whether the action was accepted |
| `done` | `bool` | Whether the game is terminal |
| `reward` | `float` | Terminal reward from the selected reward player perspective |
| `winner` | `int` | Winner id, or `-1` |
| `current_player` | `int` | Player id after the step |

The engine does not implement reward shaping. Non-terminal reward is `0.0`; win/loss terminal reward is `1.0` or `-1.0`. Compute shaped rewards in your training code if needed.

## Save And Clone

```python
snapshot = env.save_state()

# Try actions in a clone.
branch = env.clone()
branch.step_fast(int(branch.model_request_numpy()["actions"]["action_id"][0]))

# Python-style alias.
branch2 = env.copy()

# Restore.
env.load_state(snapshot)
```

## Replay Files

Use `save()` to write a portable action replay, and `load()` to recreate its
final state in any `GameEnv` instance:

```python
env.save("match.polygame")

replay = GameEnv()
final_observation = replay.load("match.polygame")
```

The shared native C++ recorder stores the effective map seed, map size, tribe
order, map-generation settings, ruleset identifier, and each accepted
`action_id`. `load()` creates the initial game again and executes the actions
in order; it rejects malformed files, incompatible rulesets, and actions that
are no longer legal. The GUI uses the same recorder and file format.

This format is intended for replays made with the same PolyEnv rules release.
See [Replays](replays.md) for format details and determinism limits.

Use `save_state()` / `load_state()` for in-memory snapshots and MCTS branches;
they are not persistent replay files.

## Fog And Tile Prediction Helpers

The package also exposes helpers for hidden tile prediction workflows:

```python
from PolyEnv import hidden_action_targets, clone_with_predictions

targets = hidden_action_targets(env)
predicted_env = clone_with_predictions(env, predictions={})
```

Use these only when your model explicitly predicts hidden tile features.
