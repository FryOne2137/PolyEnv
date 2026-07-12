# Maps And Fog Of War

PolyEnv exposes two map views. Keeping them separate prevents accidental use
of hidden information by a policy.

| API | Contains | Intended use |
| --- | --- | --- |
| `env.player_map_numpy()` | What one player can see | Model or bot input |
| `env.full_map_numpy()` | Complete ground truth | Labels, evaluation, debugging |

The default observation APIs are player-view APIs:

```python
env.observation()["tokenized_map"] == env.player_map()
env.model_request()["map_tokens"] == env.player_map()
```

## Player View

```python
visible = env.player_map_numpy()              # current player
visible_p0 = env.player_map_numpy(player_id=0)
```

Maps are flat arrays with shape `[map_width * map_height, 19]`. For a tile at
`(x, y)`:

```python
index = y * map_width + x
```

Hidden tiles have `0` in feature `visibility` and `-1` in all remaining
features. Player view also hides Metal until `Climbing`, Crops until
`Organization` or `Farming`, and Starfish until `Sailing`. Collecting Starfish requires
`Navigation` and grants 8 stars.

## Full Map

```python
truth = env.full_map_numpy()
```

This ignores fog and technology-gated visibility. Do not use it as ordinary
policy input unless the experiment is intentionally omniscient.

## Map Generation

Games with the same engine ruleset, seed, map size, tribe order, and map
type are deterministic. `env.observation()["map_type"]` reports the active
map type as `"lakes"` or `"drylands"`. Starting capitals are placed in
separate domains:

| Players | Domain grid |
| --- | --- |
| 1-4 | `2 x 2` |
| 5-9 | `3 x 3` |
| 10-16 | `4 x 4` |

Each capital is placed near the center of a distinct domain, with seeded
variation and a preference for suitable land.

For the exact meaning of each of the 19 features, see
[Token Reference](token_reference.md).
