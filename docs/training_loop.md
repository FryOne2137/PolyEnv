# Training Loop

The environment runs on CPU. Keep the model on GPU and send batched tensors to it.

The main performance rule is simple: run many environments on CPU, collate their NumPy packets, then move one batch to GPU.

`model_request_numpy()` returns the current player's visible map. It does not expose hidden tiles. If you train a hidden-map prediction model, use `env.player_map_numpy()` as input and `env.full_map_numpy()` as the supervised target.

## Single Environment Example

```python
from game_engine import GameEnv, tribes

env = GameEnv(seed=1234, map_size=11, players=(tribes.Bardur, tribes.Imperius))

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])

ok, done, reward, winner, current_player = env.step_fast(action_id)
```

The returned `reward` is terminal only: win is `1.0`, loss is `-1.0`, non-terminal steps are `0.0`. Reward shaping belongs in your training/model repository, not in the game engine.

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

This example always picks the first legal action. Replace that part with your policy.

## Map Inputs And Targets

Policy/value model input:

```python
packet = env.model_request_numpy()
visible_map = packet["map_tokens"]
legal_actions = packet["actions"]
```

Hidden-map prediction dataset:

```python
visible = env.player_map_numpy()
target = env.full_map_numpy()
```

Do not use `full_map_numpy()` as policy input unless you are intentionally running an omniscient/debug baseline.

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

## Collating Action Features

For action-scoring models, pad every per-action feature in the same way:

```python
feature_names = [
    "action_id",
    "type_id",
    "source_index",
    "target_index",
    "tech",
    "building",
    "spawn_type",
    "upgrade",
    "tile_action",
    "unit_upgrade",
    "unit_id",
    "cost_stars",
    "affordable",
    "damage_dealt",
    "damage_received",
]

batch_actions = {}
action_mask = None

for name in feature_names:
    values, mask = pad_1d([packet["actions"][name] for packet in packets])
    batch_actions[name] = values
    action_mask = mask
```

When your model returns scores with shape `[batch, max_actions]`, mask invalid actions:

```python
scores = scores.masked_fill(~action_mask.to(scores.device), -1e9)
chosen_rows = scores.argmax(dim=1).cpu()
```

Then map rows back to real action ids:

```python
for i, env in enumerate(envs):
    row = int(chosen_rows[i])
    action_id = int(batch_actions["action_id"][i, row])
    env.step_fast(action_id)
```

## CPU To GPU Guidance

- Convert NumPy to Torch with `torch.from_numpy`.
- Batch first, then call `.to("cuda")`.
- Avoid transferring one environment at a time.
- Keep the engine state on CPU.
- Send only model inputs to GPU, not the debug JSON packet.

## Resetting Finished Environments

If an environment is done, reset it before collecting the next packet:

```python
for i, env in enumerate(envs):
    if env.is_done():
        env.reset(seed=1000 + i)
```

Use your own seed schedule for reproducible training runs.
