# Training Loop

The environment runs on CPU. Keep the model on GPU and send batched tensors to it.

## Single Environment Example

```python
from game_engine import GameEnv, tribes

env = GameEnv(seed=1234, map_size=11, players=(tribes.Bardur, tribes.Imperius))

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])

ok, done, reward, winner, current_player = env.step_fast(action_id)
```

## Batched Inference Pattern

Run many environments on CPU, collate their packets, then move one batch to GPU.

```python
import torch
from game_engine import GameEnv, tribes


def make_env(seed: int) -> GameEnv:
    return GameEnv(seed=seed, map_size=11, players=(tribes.Bardur, tribes.Imperius))


envs = [make_env(seed) for seed in range(64)]

while True:
    packets = [env.model_request_numpy() for env in envs]

    map_tokens = torch.stack([
        torch.from_numpy(packet["map_tokens"])
        for packet in packets
    ])

    map_tokens = map_tokens.to("cuda", non_blocking=True)

    # logits = model(map_tokens, ...)
    # chosen_indices = logits.argmax(dim=-1).cpu().numpy()

    for env, packet in zip(envs, packets):
        legal_action_ids = packet["actions"]["action_id"]
        chosen_action_id = int(legal_action_ids[0])
        env.step_fast(chosen_action_id)
```

## Variable Number Of Actions

Every state can have a different number of legal actions. For action-based models, pad action tensors to `[batch, max_actions]` and keep a boolean valid mask.

```python
def pad_1d(arrays, pad_value=-1):
    max_len = max(len(a) for a in arrays)
    out = torch.full((len(arrays), max_len), pad_value, dtype=torch.long)
    mask = torch.zeros((len(arrays), max_len), dtype=torch.bool)
    for i, arr in enumerate(arrays):
        t = torch.from_numpy(arr)
        out[i, : len(arr)] = t
        mask[i, : len(arr)] = True
    return out, mask


action_ids, action_mask = pad_1d([
    packet["actions"]["action_id"]
    for packet in packets
])
```

Mask invalid padded actions before sampling or argmax.

