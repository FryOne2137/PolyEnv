# Model Request API

The recommended model input API is:

```python
packet = env.model_request()
```

For training and inference, prefer the NumPy variant:

```python
packet = env.model_request_numpy()
```

Both methods use the same C++ serializer path as the engine-side model client, so training code and game inference use the same packet contract.

## Packet Shape

`env.model_request()` returns Python `dict` and `list` objects:

```python
{
    "map_tokens": list[list[int]],
    "obs": dict,
    "actions": list[dict],
    "spec": dict,
}
```

`env.model_request_numpy()` keeps metadata as Python objects, but returns hot-path model fields as NumPy arrays:

```python
packet["map_tokens"]               # np.ndarray[int32], shape [tiles, 18]
packet["actions"]["action_id"]     # np.ndarray[int64], shape [num_actions]
packet["actions"]["type_id"]       # np.ndarray[int64], shape [num_actions]
packet["actions"]["source_index"]  # np.ndarray[int64], shape [num_actions]
packet["actions"]["target_index"]  # np.ndarray[int64], shape [num_actions]
packet["actions"]["arg_mask"]      # np.ndarray[uint8], shape [num_actions, 12]
```

## Map Tokens

`map_tokens` is a flat tile array. Tile index is:

```python
tile_index = y * map_width + x
```

Each tile has 18 integer features:

| Index | Name | Meaning |
| ---: | --- | --- |
| 0 | `visibility` | `1` if visible to the perspective player, else `0` |
| 1 | `is_cloak_around` | `1` if an own unit detects a hidden enemy nearby |
| 2 | `unit_hp` | Unit HP, or `-1` |
| 3 | `unit_owner` | Unit owner player id, or `-1` |
| 4 | `unit_id` | Unit id, or `-1` |
| 5 | `own_unit_kills` | Kill count for own unit, or `-1` |
| 6 | `territory_city_id` | Territory city id, or `-1` |
| 7 | `road_bridge` | Road/bridge enum id |
| 8 | `building` | Building enum id |
| 9 | `capital_layer` | `-1` none, `0` city, `1` capital |
| 10 | `city_level` | City level, or `-1` |
| 11 | `settlement_type` | Settlement enum id |
| 12 | `settlement_id` | Settlement/city id, or `-1` |
| 13 | `city_owner` | City owner player id, or `-1` |
| 14 | `city_units_occupied` | Own city unit count, or `-1` |
| 15 | `resource` | Resource enum id |
| 16 | `base_terrain` | Base terrain enum id |
| 17 | `tribe` | Tribe enum id |

## Observation Fields

`packet["obs"]` contains scalar game and player state:

```python
{
    "turn": int,
    "current_player": int,
    "game_over": bool,
    "winner": int,
    "map_size": int,
    "player_stars": int,
    "owns_units": int,
    "own_cities": int,
    "next_turn_star_income": int,
    "pending_city_upgrade": bool,
    "pending_city_id": int,
    "pending_upgrade_a": int,
    "pending_upgrade_b": int,
}
```

## Actions

The model must choose one of the legal `action_id` values from `packet["actions"]`.

Each action in `model_request()` contains:

```python
{
    "action_id": int,
    "type": str,
    "type_fullname": str,
    "type_id": int,
    "source_index": int,
    "target_index": int,
    "city": int,
    "tech": int,
    "building": int,
    "spawn_type": int,
    "upgrade": int,
    "tile_action": int,
    "unit_upgrade": int,
    "unit_id": int,
    "cost_stars": int,
    "stars_before": int,
    "affordable": int,
    "damage_dealt": int,
    "damage_received": int,
    "arg_mask": list[int],
}
```

`arg_mask` follows the order in `packet["spec"]["arg_order"]`:

```python
[
    "source_index",
    "target_index",
    "tech",
    "building",
    "spawn_type",
    "upgrade",
    "tile_action",
    "unit_upgrade",
    "unit_id",
    "damage_dealt",
    "damage_received",
    "population_gain",
]
```

## Step With A Chosen Action

```python
packet = env.model_request_numpy()
action_ids = packet["actions"]["action_id"]

chosen_action_id = int(action_ids[0])
ok, done, reward, winner, current_player = env.step_fast(chosen_action_id)
```

Never invent action ids. Always choose an id from the current legal action list.

## Torch Conversion

NumPy arrays can be wrapped by Torch cheaply on CPU:

```python
import torch

packet = env.model_request_numpy()

map_tokens = torch.from_numpy(packet["map_tokens"])
action_ids = torch.from_numpy(packet["actions"]["action_id"])
action_type = torch.from_numpy(packet["actions"]["type_id"])
```

Move batches to GPU after collation:

```python
map_tokens = map_tokens.to("cuda", non_blocking=True)
```

