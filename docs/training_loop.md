# Training Pattern

PolyEnv runs game logic on CPU. Keep the model on GPU and batch model inputs
from many environments.

```python
import torch
from PolyEnv import GameEnv, Bardur, Imperius, Lakes

envs = [GameEnv(seed=seed, map_size=11, players=(Bardur, Imperius), map_type=Lakes)
        for seed in range(64)]

packets = [env.model_request_numpy() for env in envs]
maps = torch.stack([torch.from_numpy(p["map_tokens"]) for p in packets])
maps = maps.to("cuda", non_blocking=True)

# scores = model(maps, ...)
for env, packet in zip(envs, packets):
    action_id = int(packet["actions"]["action_id"][0])
    env.step_fast(action_id)
```

Each state has a different number of legal actions. Pad per-action arrays when
batching action scores, keep a valid-row mask, and convert a chosen row back to
`packet["actions"]["action_id"][row]` before stepping.

Use `packet["map_tokens"]` or `player_map_numpy()` for policy input.
`full_map_numpy()` is ground truth for debugging or supervised hidden-map
prediction, not ordinary policy input.

Use `clone()` for MCTS branches. PolyEnv returns only terminal reward; reward
shaping belongs in the training code.
