# Hidden-Map Predictions

`hidden_action_targets()` and `clone_with_predictions()` are module-level
functions imported from `PolyEnv`; they are **not** methods of `GameEnv`.
They support MCTS or other rollouts that use model predictions for tiles under
fog of war. Normal policies should use only the player-view packet and do not
need these helpers.

## `hidden_action_targets(env, hidden_value=-1)`

Returns a `set[int]` of tile indices that are both hidden from the current
player and targets of a currently legal parameterized action. Predicting only
these tiles keeps the prediction budget small and limits predictions to tiles
that can affect the next rollout decision. An empty set means that no hidden
legal target exists.

| Element | Type | Meaning |
| --- | --- | --- |
| `env` | `GameEnv` | Game state whose current player's fog of war is inspected. |
| return value | `set[int]` | Map tile indices, for example `{18, 51}`. |
| `hidden_value` | `int` | Kept for API compatibility; hidden tiles use `-1` in player-view maps. |

Tile indices address rows in `packet["map_tokens"]`: `targets` contains only
values from `0` through `len(packet["map_tokens"]) - 1`.

## `clone_with_predictions(env, predictions, perspective=None)`

Creates an independent clone of `env`, then applies the sparse mapping
`{tile_index: feature_vector}` to hidden tiles in that clone. The original
environment is never mutated. `perspective` selects whose fog-of-war mask is
used; by default it is the current player.

Each feature vector has 18 integer entries in the same layout as
`map_tokens`. Only safe terrain-level values are applied: road/bridge,
building, non-city settlement type, resource, base terrain, and tribe.
Visibility, units, city ownership, and territory ownership are not replaced.
Predictions for visible tiles are ignored.

| Element | Type | Meaning |
| --- | --- | --- |
| `env` | `GameEnv` | Source environment. It is not modified. |
| `predictions` | `dict[int, list[int]]` | Sparse mapping from a tile index to exactly one 18-integer feature vector. |
| `perspective` | `int \| None` | Player id whose hidden tiles may be changed. `None` means the current player. |
| return value | `GameEnv` | Separate clone used for a rollout. |

## Complete example

The following example predicts a land tile containing fruit and applies the
prediction only if that tile is a hidden, legal action target:

```python
from PolyEnv import clone_with_predictions, hidden_action_targets

packet = env.model_request_numpy()
targets = hidden_action_targets(env)

if targets:
    tile_index = next(iter(targets))

    # Every prediction has 18 values: the same layout as map_tokens[tile_index].
    predicted_tile = [-1] * 18
    predicted_tile[7] = 0    # road_bridge: none
    predicted_tile[8] = 0    # building: none
    predicted_tile[11] = 0   # settlement_type: none
    predicted_tile[15] = 3   # resource: fruit
    predicted_tile[16] = 2   # base_terrain: land
    predicted_tile[17] = 3   # tribe: Bardur

    predictions = {tile_index: predicted_tile}
    rollout_env = clone_with_predictions(env, predictions)

    assert rollout_env is not env
    # Use only the clone for simulated actions.
    action_id = int(rollout_env.model_request_numpy()["actions"]["action_id"][0])
    rollout_env.step_fast(action_id)
```

If `targets` is empty, do not call the predictor or clone with a map
prediction; no currently legal action can benefit from it.

## Feature-vector layout

The vector must contain 18 integers. The engine reads only the following
positions when applying a prediction; all other entries may be set to `-1`.

| Vector index | Field | Example | Values |
| ---: | --- | ---: | --- |
| 7 | `road_bridge` | `0` | 0 none, 1 road, 2 bridge, 3 water connection |
| 8 | `building` | `0` | Building id; 0 means none |
| 11 | `settlement_type` | `0` | 0 none, 1 village, 3 starfish, 4 ruin; city is not applied |
| 15 | `resource` | `3` | Resource id; 3 is fruit |
| 16 | `base_terrain` | `2` | 0 ocean, 1 water, 2 land, 3 mountain, 4 forest |
| 17 | `tribe` | `3` | Tribe id; 3 is Bardur |

For the complete ids and the meaning of every `map_tokens` column, see
[Game Data And IDs](token_reference.md).
