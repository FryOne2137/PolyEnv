# VectorGameEnv: Native Batched Training

`VectorGameEnv` is the high-throughput API for reinforcement-learning
training. It owns many independent `GameSession` instances, executes them on
a persistent C++ worker pool, and returns one dense NumPy batch per call.

The hot path does **not** build JSON, Python dictionaries per game, strings,
or a Python loop over environments. `GameEnv` remains the right API for
debugging, replays, MCTS branches, and one-off games.

## Create A Batch

```python
import numpy as np

from PolyEnv import Bardur, Imperius, VectorGameEnv

env = VectorGameEnv(
    num_envs=256,
    seed=1234,
    map_size=11,
    players=(Bardur, Imperius),
    num_threads=8,       # None: use up to the logical CPU count
    max_actions=512,
    auto_reset=True,
    visible_event_history=16,  # last K visible world events per environment
)

batch = env.reset(seed=1234)
```

The initial seed of environment `i` is `seed + i`. Calling `reset(seed=x)`
restarts that same deterministic sequence. A zero seed retains the normal
single-environment random-seed behavior.

## Batch Layout

| Key | Dtype | Shape | Meaning |
| --- | --- | --- | --- |
| `map_tokens` | `int32` | `[B, tiles, 23]` | Visible player map |
| `state` | `int32` | `[B, 11]` | Compact scalar state, layout below |
| `action_id` | `int32` | `[B, Amax]` | Action-space id for each padded row |
| `action_features` | `int32` | `[B, Amax, 17]` | Numeric fields for each action row |
| `action_arg_mask` | `uint8` | `[B, Amax, 12]` | Applicable action-argument fields |
| `action_mask` | `uint8` | `[B, Amax]` | `1` for a legal row, `0` for padding |
| `visible_event_features` | `int32` | `[B, K, 26]` | Right-aligned history of visible world events |
| `visible_event_sequence` | `uint64` | `[B, K]` | Private event-stream sequence id |
| `visible_event_action_sequence` | `uint64` | `[B, K]` | World-action id shared by observers that saw it |
| `visible_event_mask` | `uint8` | `[B, K]` | `1` for an event row, `0` for history padding |
| `visible_event_affected` | `int32` | `[B, K, 9, 8]` | Affected-unit details for every event row |
| `visible_event_affected_mask` | `uint8` | `[B, K, 9]` | Valid affected-unit rows |
| `legal_action_count` | `int32` | `[B]` | Number of legal, unpadded rows |
| `reward` | `float32` | `[B]` | Terminal reward from the preceding action |
| `terminated` | `uint8` | `[B]` | Terminal flag from the preceding action |
| `action_valid` | `uint8` | `[B]` | Whether the supplied action id was legal |
| `env_id` | `int32` | `[B]` | Stable row-to-environment index |

`B` is `num_envs`, `tiles` is `map_size * map_size`, and `Amax` is
`max_actions`. `K` is `visible_event_history`. Padding action rows have
`action_id == -1` and `action_mask == 0`.
On `reset()`, `action_valid`, `reward`, and `terminated` are all zero because
no action has yet been supplied.

The `state` fields are, in order:

```text
turn, current_player, game_over, winner, player_stars,
owns_units, own_cities, next_turn_star_income,
pending_city_id, pending_upgrade_a, pending_upgrade_b
```

The `action_features` fields are, in order:

```text
type_id, source_index, target_index, city, tech, building, spawn_type,
upgrade, tile_action, unit_upgrade, unit_id, population_gain, cost_stars,
stars_before, affordable, damage_dealt, damage_received
```

For maximum throughput `include_combat_preview` defaults to `False`; in that
mode the final two action fields are `-1`. Set it to `True` only if the policy
needs exact attack previews: computing one requires a simulated combat branch
per attack action.

## Visible Event Window

Set `visible_event_history=K` to include the last `K` world events observable
by the player whose observation is in that batch row. This uses exactly the
same fog-of-war filtering as `GameEnv.visible_events_numpy()`: hidden source
positions, hidden units and hidden victims are never reconstructed from the
chosen action id.

The valid event rows are chronological and **right-aligned**. Thus the newest
event is always at index `K - 1`; use `visible_event_mask` to ignore left-side
padding. `visible_event_features` has this layout, also available through
`env.visible_event_feature_names`:

```text
round, turn, type_id, flags, source_index, target_index, damage, hp_before,
hp_after, actor_player, actor_tribe, tile_action_kind, building_type,
spawn_type, source_unit_type, target_unit_type, source_observed_unit_id,
target_observed_unit_id, source_unit_hp_before, source_unit_hp_after,
target_unit_hp_before, target_unit_hp_after, unit_upgrade_kind,
upgraded_unit_type, unit_destroyed, source_unit_destroyed
```

`visible_event_affected` has at most nine rows per event because an attack can
touch only its target tile and the eight neighboring tiles. Its feature order
is exposed as `env.visible_event_affected_feature_names` and is:

```text
observed_unit_id, tile_index, unit_type, damage, hp_before, hp_after,
destroyed, splash
```

The default `visible_event_history=0` disables history encoding to keep the
fastest existing training path. The event keys are still returned with a
zero-sized `K` dimension, so batch schemas stay stable. With a positive `K`,
the native vector implementation retains only the last `K` private records of
each player; memory remains bounded for long-running training. It is a model
input window, not an unbounded replay log. Use scalar `GameEnv` and
`visible_events_numpy(cursor)` when a complete cursor-based journal is needed.

When `auto_reset=True`, a terminal row is reset before its observation is
encoded. Its returned visible-event window is therefore the empty history of
the new episode, while `reward` and `terminated` still describe the terminal
transition.

## Select And Step Batched Actions

```python
import numpy as np
import torch

device = "cuda"

while True:
    maps = torch.from_numpy(batch["map_tokens"]).to(device)
    action_features = torch.from_numpy(batch["action_features"]).to(device)
    valid = torch.from_numpy(batch["action_mask"]).to(device, dtype=torch.bool)

    scores = policy(maps, action_features)       # [B, Amax]
    rows = scores.masked_fill(~valid, -torch.inf).argmax(dim=1).cpu().numpy()

    env_rows = np.arange(env.num_envs)
    action_ids = batch["action_id"][env_rows, rows]
    batch = env.step(action_ids)
```

Always choose an id from the current batch. The action rows and ids change
after every step.

When `auto_reset=True` (the default), a terminal environment is reset inside
the same `step` call. `reward` and `terminated` still describe the terminal
transition, while that row's returned observation is already the next
episode's initial state. This is appropriate for standard rollout buffers:
store the transition first, then use the returned row for the next action.

## Capacity And Throughput

`max_actions` is a fixed batch capacity, not a truncation setting. If an
environment has more legal actions than the configured capacity,
`VectorGameEnv` raises an error instead of silently dropping legal moves.
Start with `512`, collect the maximum observed `legal_action_count` across
your training seeds, then set a small safety margin. Every environment in one
vector must use the same map size and action capacity.

Set `num_threads` to the number of CPU cores reserved for simulation. Avoid
wrapping `VectorGameEnv.step()` in a Python thread pool: the environment
already performs its parallel work in C++ and releases the GIL while it runs.

The arrays returned by `reset()` and `step()` are independent NumPy arrays, so
they are safe to retain in a replay or rollout buffer. For GPU transfer,
collate only these dense batches; a `non_blocking=True` transfer needs pinned
host memory to overlap with GPU work.
