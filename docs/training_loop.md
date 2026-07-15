# Training Pattern

For throughput-oriented RL training, use [`VectorGameEnv`](vector_env.md).
It batches simulation, observation encoding, and legal-action enumeration in
C++; do not create a Python list of `GameEnv` instances for the hot path.

```python
import numpy as np
import torch

from PolyEnv import Bardur, Imperius, VectorGameEnv

env = VectorGameEnv(
    num_envs=256,
    seed=1234,
    map_size=11,
    players=(Bardur, Imperius),
    num_threads=8,
    max_actions=512,
)
batch = env.reset()

while True:
    maps = torch.from_numpy(batch["map_tokens"]).to("cuda")
    actions = torch.from_numpy(batch["action_features"]).to("cuda")
    valid = torch.from_numpy(batch["action_mask"]).to("cuda", dtype=torch.bool)

    scores = model(maps, actions)
    rows = scores.masked_fill(~valid, -torch.inf).argmax(dim=1).cpu().numpy()
    action_ids = batch["action_id"][np.arange(env.num_envs), rows]
    batch = env.step(action_ids)
```

`action_mask` must be applied before selecting a row. The returned
`action_id`, not the padded row number, is passed back to `step()`.

`GameEnv` remains useful for debugging and MCTS. Its
`model_request_numpy()` API is intentionally human-friendly, but it has
per-environment Python/JSON overhead and is not the highest-throughput
training path.

See [VectorGameEnv: Native Batched Training](vector_env.md) for the batch
layout, reset behavior, action-capacity rules, and GPU-transfer guidance.
