# Model Input And Actions

Use `model_request_numpy()` as the normal interface between PolyEnv and model
code. It contains the current player's visible state and every currently legal
action.

```python
packet = env.model_request_numpy()

map_tokens = packet["map_tokens"]
actions = packet["actions"]
action_id = int(actions["action_id"][0])

ok, done, reward, winner, current_player = env.step_fast(action_id)
```

Use `model_request()` when a readable list/dict packet is more useful than
NumPy arrays.

## Packet

```python
packet = {
    "map_tokens": np.ndarray,  # [tiles, 18], player view
    "obs": dict,               # turn, player, stars, cities, income, ...
    "actions": dict,           # one row per legal action
    "spec": dict,              # dimensions and categorical vocabularies
}
```

Useful action arrays are:

| Field | Meaning |
| --- | --- |
| `action_id` | Value passed to `step_fast()` |
| `type_id` | Action category |
| `source_index`, `target_index` | Tile indices, or `-1` when irrelevant |
| `unit_id`, `city` | Affected object id, or `-1` |
| `tech`, `building`, `spawn_type` | Chosen categorical argument |
| `cost_stars`, `stars_before`, `affordable` | Cost information |
| `damage_dealt`, `damage_received` | Combat preview when applicable |
| `arg_mask` | Which action arguments apply |

The `obs` dictionary includes turn, current player, winner/game-over state,
map size, `map_type` (`"lakes"` or `"drylands"`), stars, own units/cities,
income, and pending city reward state.

## Legal Actions

`actions["action_id"]` contains real action-space ids, not row numbers. A
model that scores action rows must map its chosen row back to the id:

```python
scores = policy(packet)  # one score per action row
row = int(scores.argmax())
action_id = int(packet["actions"]["action_id"][row])
env.step_fast(action_id)
```

Never invent an action id or reuse one after the state changes.

## Torch

```python
import torch

packet = env.model_request_numpy()
map_tokens = torch.from_numpy(packet["map_tokens"])
```

For many environments, collate CPU arrays first and transfer one batch to the
GPU. Keep game states and `GameEnv` objects on CPU.

## Map Features

`map_tokens` has shape `[map_width * map_height, 18]`. It is always the
player-view map; hidden tiles are masked. See [Maps And Fog Of War](map_api.md)
and [Token Reference](token_reference.md) for feature layout and enum ids.
