# PolyEnv Game Engine

PolyEnv is an unofficial Polytopia-like game engine with Python bindings for AI training and inference experiments.

## Attribution

The map generation code is based on [QuasiStellar/Polytopia-Map-Generator](https://github.com/QuasiStellar/Polytopia-Map-Generator) and has been modified for this engine.

## Quick Start

The main Python entry point is `GameEnv`. It exposes game state, legal actions, and fast stepping APIs. For model integration, use the canonical model request methods:

```python
from game_engine import GameEnv, Bardur, Imperius

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))

packet = env.model_request()
fast_packet = env.model_request_numpy()
```

Use `model_request()` for debugging and JSON-like inspection. Use `model_request_numpy()` for training or batched inference.

`model_request*` returns the current player's visible map. Full ground-truth map access is explicit through `env.full_map()` and `env.full_map_numpy()`.

## Minimal Rollout

```python
from game_engine import GameEnv, Bardur, Imperius

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))

while not env.is_done():
    packet = env.model_request_numpy()
    legal_action_ids = packet["actions"]["action_id"]

    # Replace this with your policy/model.
    action_id = int(legal_action_ids[0])

    ok, done, reward, winner, current_player = env.step_fast(action_id)
    if not ok:
        raise RuntimeError(f"Illegal action selected: {action_id}")
```

## Which API Should I Use?

| Use case | Method |
| --- | --- |
| Inspect state by hand | `env.model_request()` |
| Train or run a model | `env.model_request_numpy()` |
| Read the current player's visible map | `env.player_map_numpy()` |
| Read the full ground-truth map | `env.full_map_numpy()` |
| Step with a chosen legal action id | `env.step_fast(action_id)` |
| Debug one action id | `env.decode_action(action_id)` |
| Read a simple visible observation | `env.observation()` |

## Important Rule

Models must choose an `action_id` from the current packet:

```python
packet["actions"]["action_id"]
```

Do not invent action ids. The legal action set changes after every step.

## Pages

- [Installation](installation.md): install from GitHub or a local checkout.
- [Python API](python_api.md): core `GameEnv` methods.
- [Map API](map_api.md): player-view maps, full maps, and hidden-tile prediction workflow.
- [Model Request API](model_request_api.md): packet schema, map tokens, actions, and NumPy layout.
- [Token Reference](token_reference.md): numeric ids for terrain, resources, buildings, tribes, techs, actions, and units.
- [Training Loop](training_loop.md): CPU environments, NumPy packets, Torch tensors, and GPU batching.
- [Troubleshooting](troubleshooting.md): common install, build, and Read the Docs failures.
