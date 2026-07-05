# Map API

The engine exposes two map views with different purposes:

| API | Meaning | Use |
| --- | --- | --- |
| `env.player_map()` | Current player's visible map | Bot/model input |
| `env.player_map_numpy()` | Current player's visible map as `np.ndarray[int32]` | Training/inference input |
| `env.full_map()` | Full ground-truth map | Dataset labels, debugging, evaluation |
| `env.full_map_numpy()` | Full ground-truth map as `np.ndarray[int32]` | Hidden-tile prediction targets |

The default API is intentionally player-view:

```python
env.observation()["tokenized_map"] == env.player_map()
env.tokenized_map() == env.player_map()
env.model_request()["map_tokens"] == env.player_map()
env.model_request_numpy()["map_tokens"] == env.player_map_numpy()
```

Use `full_map*` only when you explicitly need the true hidden state.

## Player View

`player_map()` returns only what the selected player has observed.

```python
visible = env.player_map()
visible_np = env.player_map_numpy()
```

By default it uses the current player. You can request another perspective:

```python
visible_p0 = env.player_map(player_id=0)
visible_p0_np = env.player_map_numpy(player_id=0)
```

Hidden tiles keep feature `0` as the visibility flag and mask features `1..17` with `hidden_value`:

```python
visible = env.player_map(hidden_value=-1)
```

A hidden tile therefore looks like:

```text
[0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]
```

Player-view also applies game knowledge gates:

| Hidden content | Becomes visible when player has |
| --- | --- |
| Metal | `Climbing` |
| Crops | `Organization` |
| Starfish | `Sailing` |

Enemy hidden units are also masked unless the engine says the player can observe/detect them.

## Full Map

`full_map()` returns ground truth.

```python
target = env.full_map()
target_np = env.full_map_numpy()
```

This map does not apply:

- fog-of-war masking
- `hidden_value`
- technology-gated resource hiding
- hidden-unit masking

It is meant for supervised targets and debugging, not for the policy input.

## Tile Layout

All map methods return a flat tile array with shape:

```text
[map_width * map_height, 18]
```

Tile index conversion:

```python
idx = y * map_width + x
x = idx % map_width
y = idx // map_width
```

Each tile has the same 18 features documented in [Model Request API](model_request_api.md#map-tokens).

## Prediction Workflow

A typical hidden-map prediction setup uses player-view as input and full-map as target:

```python
visible = env.player_map_numpy()
target = env.full_map_numpy()
```

For external MCTS:

```python
root = env.clone()
packet = root.model_request_numpy()

# Your model predicts hidden tile features outside this repo.
predictions = {}

branch = root.copy()
branch._apply_tile_predictions(predictions)
```

Reward shaping is intentionally not part of this engine API. Compute shaped rewards in your training/model repository from observations, full-map labels, or step metadata as needed.
