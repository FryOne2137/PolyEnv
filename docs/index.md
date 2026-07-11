# PolyEnv

PolyEnv is an unofficial Polytopia-like game engine with a C++ core and Python
bindings. It is intended for AI training, external bots, MCTS, and inspecting
saved games.

The supported ruleset contains the 12 regular tribes. Aquarion, Elyrion,
Polaris, and Cymanti are not supported.

## Start Here

```python
from PolyEnv import GameEnv, Bardur, Imperius

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])
ok, done, reward, winner, current_player = env.step_fast(action_id)
```

Use only action ids returned in the current packet. The legal set changes after
every step.

## Important Concepts

| Need | API |
| --- | --- |
| Model input and legal actions | `env.model_request_numpy()` |
| Readable debug packet | `env.model_request()` |
| Player-view map | `env.player_map_numpy()` |
| Full ground-truth map | `env.full_map_numpy()` |
| Fast action execution | `env.step_fast(action_id)` |
| MCTS branch | `env.clone()` |
| Portable replay | `env.save(path)` / `env.load(path)` |

The normal model and observation APIs expose only the current player's view.
`full_map_numpy()` is deliberately separate and is meant for debugging or
supervised hidden-map prediction.

## Next Pages

1. [Installation](installation.md)
2. [Core Python API](python_api.md)
3. [Model input and actions](model_request_api.md)
4. [Maps and fog of war](map_api.md)
5. [Replays](replays.md) and [GUI](gui.md)

## Attribution

Map generation is based on
[QuasiStellar/Polytopia-Map-Generator](https://github.com/QuasiStellar/Polytopia-Map-Generator)
and was modified for PolyEnv.
