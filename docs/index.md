# PolyEnv Game Engine

PolyEnv is an unofficial Polytopia-like game engine with Python bindings for AI training and inference experiments.

The main Python entry point is `GameEnv`. It exposes game state, legal actions, and fast stepping APIs. For model integration, prefer the canonical model request methods:

```python
from game_engine import GameEnv, tribes

env = GameEnv(seed=1234, map_size=11, players=(tribes.Bardur, tribes.Imperius))

packet = env.model_request()
fast_packet = env.model_request_numpy()
```

Use `model_request()` for debugging and JSON-like inspection. Use `model_request_numpy()` for training or batched inference.

## Documentation

- [Installation](installation.md): install from GitHub or a local checkout.
- [Model Request API](model_request_api.md): packet schema, map tokens, actions, and NumPy layout.
- [Training Loop](training_loop.md): basic CPU/GPU batching pattern.

