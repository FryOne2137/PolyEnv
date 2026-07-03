# Python API

This page lists the core Python API used by training and inference code.

## Create An Environment

```python
from game_engine import GameEnv, tribes

env = GameEnv(
    seed=1234,
    map_size=11,
    players=(tribes.Bardur, tribes.Imperius),
)
```

You can also pass tribe names through `get_tribe`:

```python
from game_engine import GameEnv, get_tribe

env = GameEnv(
    seed=1234,
    map_size=11,
    players=(get_tribe("Bardur"), get_tribe("Imperius")),
)
```

## State Methods

| Method | Returns | Use |
| --- | --- | --- |
| `env.model_request()` | `dict` with `map_tokens`, `obs`, `actions`, `spec` | Debuggable canonical model packet |
| `env.model_request_numpy()` | `dict` with NumPy arrays for hot-path fields | Training and inference |
| `env.observation()` | `dict` | Human/debug observation |
| `env.tokenized_map()` | `list[list[int]]` | Map tokens only |
| `env.current_player()` | `int` | Current player id |
| `env.is_done()` | `bool` | Terminal state check |

## Action Methods

| Method | Returns | Use |
| --- | --- | --- |
| `env.legal_param_actions()` | `list[dict]` | Debug legal actions |
| `env.legal_action_ids_fast()` | `list[int]` | Fast legal action ids |
| `env.decode_action(action_id)` | `dict` | Inspect one legal action id |
| `env.action_param_spec()` | `dict` | Vocab sizes and argument order |
| `env.step_fast(action_id)` | tuple | Fast environment step |
| `env.step(action_id)` | `dict` | Step with detailed output |

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

## Save And Clone

```python
snapshot = env.save_state()

# Try actions in a clone.
branch = env.clone()
branch.step_fast(int(branch.model_request_numpy()["actions"]["action_id"][0]))

# Restore.
env.load_state(snapshot)
```

## Fog And Tile Prediction Helpers

The package also exposes helpers for hidden tile prediction workflows:

```python
from game_engine import hidden_action_targets, clone_with_predictions

targets = hidden_action_targets(env)
predicted_env = clone_with_predictions(env, predictions={})
```

Use these only when your model explicitly predicts hidden tile features.

