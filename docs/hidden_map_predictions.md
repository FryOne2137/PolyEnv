# Hidden-Map Predictions

`clone_with_predictions()` is a module-level function imported from `PolyEnv`;
it is **not** a method of `GameEnv`. It supports MCTS or other rollouts that
use model predictions for tiles under fog of war. Normal policies should use
only the player-view packet and do not need this helper.

## Function

### `clone_with_predictions(env, predictions, perspective=None)`

Creates an independent clone of `env`, then applies the sparse mapping
`{tile_index: feature_vector}` to hidden tiles in that clone. The original
environment is never mutated. `perspective` selects whose fog-of-war mask is
used; by default it is the current player.

Each feature vector needs at least 19 integer entries in the same layout as
`map_tokens`. Only safe terrain-level values are applied: road/bridge,
building, non-city settlement type, resource, base terrain, and tribe.
Visibility, units, city ownership, and territory ownership are not replaced.
Predictions for visible tiles are ignored.

| Element | Type | Meaning |
| --- | --- | --- |
| `env` | `GameEnv` | Source environment. It is not modified. |
| `predictions` | `dict[int, list[int] \| np.ndarray]` | Sparse mapping from a tile index to one feature vector with at least 19 integer values. |
| `perspective` | `int \| None` | Player id whose hidden tiles may be changed. `None` means the current player. |
| return value | `GameEnv` | Separate clone used for a rollout. |

## Workflow example

The following example creates a rollout clone with a hypothesis that one
currently hidden tile contains land and fruit:

```python
import numpy as np

from PolyEnv import clone_with_predictions

packet = env.model_request_numpy()
hidden_tiles = env.hidden_tile_indices()

if hidden_tiles:
    tile_index = hidden_tiles[0]

    # A one-dimensional NumPy array is accepted as the feature vector.
    # It has the same 19-value layout as map_tokens[tile_index].
    predicted_tile = np.full(19, -1, dtype=np.int32)
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

Legal actions in PolyEnv never target hidden tiles. This helper does not
change action legality and does not reveal a tile to the player; it only
changes the internal terrain hypothesis of the clone.

### What `predictions` is

`predictions` is a sparse Python dictionary. Its key is an integer map tile
index and its value is that tile's predicted 19-value feature vector:

```python
predictions = {
    18: [-1, -1, -1, -1, -1, -1, -1, 0, 0, -1, -1, 0, -1, -1, -1, 3, 2, 3, -1],
    51: np.array([-1] * 19, dtype=np.int32),
}
```

The dictionary itself cannot be replaced with a two-dimensional NumPy array:
the engine needs the key to know which map tile each vector belongs to. A
one-dimensional integer NumPy array is accepted for an individual dictionary
value, for example `predictions[tile_index] = model_output[i].astype(np.int32)`.
If a model outputs `model_output` with shape `[number_of_predicted_tiles, 19]`,
keep a stable tile order and build the mapping explicitly:

```python
tile_indices = sorted(env.hidden_tile_indices())
# model_output[row] is the prediction for tile_indices[row]
predictions = {
    int(tile_index): model_output[row].astype(np.int32)
    for row, tile_index in enumerate(tile_indices)
}
```

## Feature-vector layout

The vector must contain at least 19 integers. The engine reads only the
following positions when applying a prediction; all other entries may be set
to `-1`.

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
