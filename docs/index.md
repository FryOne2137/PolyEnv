# PolyEnv Game Engine

PolyEnv is an unofficial Polytopia-like game engine with Python bindings for AI training and inference experiments.

The main Python entry point is `GameEnv`. It exposes game state, legal actions, and fast stepping APIs. For model integration, use the canonical model request methods:

```python
from game_engine import GameEnv, tribes

env = GameEnv(seed=1234, map_size=11, players=(tribes.Bardur, tribes.Imperius))

packet = env.model_request()
fast_packet = env.model_request_numpy()
```

Use `model_request()` for debugging and JSON-like inspection. Use `model_request_numpy()` for training or batched inference.

## Minimal Rollout

```python
from game_engine import GameEnv, tribes

env = GameEnv(seed=1234, map_size=11, players=(tribes.Bardur, tribes.Imperius))

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
| Step with a chosen legal action id | `env.step_fast(action_id)` |
| Debug one action id | `env.decode_action(action_id)` |
| Read a simple full observation | `env.observation()` |

## Important Rule

Models must choose an `action_id` from the current packet:

```python
packet["actions"]["action_id"]
```

Do not invent action ids. The legal action set changes after every step.

## Pages

- [Installation](installation.md): install from GitHub or a local checkout.
- [Python API](python_api.md): core `GameEnv` methods.
- [Model Request API](model_request_api.md): packet schema, map tokens, actions, and NumPy layout.
- [Token Reference](token_reference.md): numeric ids for terrain, resources, buildings, tribes, techs, actions, and units.
- [Training Loop](training_loop.md): CPU environments, NumPy packets, Torch tensors, and GPU batching.
- [Troubleshooting](troubleshooting.md): common install, build, and Read the Docs failures.
