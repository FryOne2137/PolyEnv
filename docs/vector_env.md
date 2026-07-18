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
| `legal_action_count` | `int32` | `[B]` | Number of represented, unpadded action rows (`<= Amax`) |
| `total_legal_action_count` | `int32` | `[B]` | Legal actions before the fixed-capacity truncation |
| `action_truncated` | `uint8` | `[B]` | `1` when not every legal action fit in the batch |
| `reward` | `float32` | `[B]` | Terminal reward from the preceding action |
| `terminated` | `uint8` | `[B]` | Terminal flag from the preceding action |
| `action_valid` | `uint8` | `[B]` | Whether the supplied action id was legal |
| `env_id` | `int32` | `[B]` | Stable row-to-environment index |
| `state_id` | `uint64` | `[B]` | Opaque id for this exact live position |
| `episode_id` | `uint64` | `[B]` | Opaque id for the current episode in that environment |

`B` is `num_envs`, `tiles` is `map_size * map_size`, and `Amax` is
`max_actions`. `K` is `visible_event_history`. Padding action rows have
`action_id == -1` and `action_mask == 0`.
On `reset()`, `action_valid`, `reward`, and `terminated` are all zero because
no action has yet been supplied.

`state_id` changes after every accepted action and every reset. `episode_id`
changes only when a new episode begins, including an automatic reset after a
terminal action. They are identifiers, not model features: preserve them with
the batch and submit them unchanged to a checked step when inference can be
asynchronous.

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

`max_actions` is a fixed batch capacity. When a position has more legal
actions, PolyEnv exposes a deterministic subset instead of stopping training:
it keeps the canonical prefix and, if necessary, reserves the final row for
`EndTurn`. Thus every truncated row still has a progress action. Inspect
`action_truncated` and `total_legal_action_count`: truncation changes the
policy target and can reduce playing strength, so set `max_actions` high enough
that it is rare in normal training. Every environment in one vector must use
the same map size and action capacity.

Set `num_threads` to the number of CPU cores reserved for simulation. Avoid
wrapping `VectorGameEnv.step()` in a Python thread pool: the environment
already performs its parallel work in C++ and releases the GIL while it runs.

The arrays returned by `reset()` and `step()` are independent NumPy arrays, so
they are safe to retain in a replay or rollout buffer. For GPU transfer,
collate only these dense batches; a `non_blocking=True` transfer needs pinned
host memory to overlap with GPU work.

## Reusable Pinned Buffers And Asynchronous Transfer

`reset()` and `step()` allocate a fresh NumPy batch. For a long-running
GPU-backed trainer, use `batch_spec()`, `reset_into()` and `step_into()` to
reuse caller-owned arrays instead. The native engine writes them
synchronously without an output copy or dtype conversion, and retains no
pointer after the method returns. This keeps PolyEnv independent of PyTorch,
CUDA and the model repository while allowing its output to be backed by pinned
host memory.

Every field in the batch needs an array with the exact dtype and shape in
`batch_spec()`. Output arrays must be writable, aligned, C-contiguous, and
must not overlap. `step_into()` requires an `int32[B]` action array; it copies
that small vector before writing the next batch, so a strided selection such
as `buffers["action_id"][:, 0]` is safe.

```python
import numpy as np
import torch

# Pin only data that the model actually reads. Metadata can remain ordinary
# NumPy memory, which avoids needlessly pinning large amounts of system RAM.
model_fields = {
    "map_tokens", "state", "action_features", "action_arg_mask", "action_mask",
    "visible_event_features", "visible_event_mask", "visible_event_affected",
    "visible_event_affected_mask",
}
torch_dtypes = {
    np.dtype(np.int32): torch.int32,
    np.dtype(np.uint8): torch.uint8,
}

host_tensors = {}       # Keep these owners alive for as long as buffers are used.
buffers = {}
for name, field in env.batch_spec().items():
    shape, dtype = tuple(field["shape"]), np.dtype(field["dtype"])
    if name in model_fields:
        host_tensors[name] = torch.empty(
            shape, dtype=torch_dtypes[dtype], device="cpu", pin_memory=True,
        )
        buffers[name] = host_tensors[name].numpy()  # zero-copy CPU view
    else:
        buffers[name] = np.empty(shape, dtype=dtype)

env.reset_into(buffers, seed=1234)

# Reuse the same storage every rollout step.
action_ids = buffers["action_id"][:, 0]
env.step_into(action_ids, buffers)
```

`torch.from_numpy()` is also a zero-copy CPU view when an external trainer
already owns NumPy buffers. DLPack is unnecessary for this path and does not
remove the physical host-to-device transfer.

For real overlap, allocate at least two complete buffer slots. Start each H2D
copy on a dedicated CUDA copy stream with `non_blocking=True`, record an event,
and give the CPU a different slot while the first transfer runs. Do not call
`reset_into()` or `step_into()` on a slot until the event covering the last GPU
read of its pinned host tensors has completed. Likewise, wait for an
asynchronous GPU-to-CPU action copy before passing its action array to
`step_into()`.

```python
# Sketch: `slot` owns `buffers`, `host_tensors`, a copy stream and copy_done.
# Pick only a slot whose previous copy_done event has completed.
env.step_into(action_ids, slot.buffers)
with torch.cuda.stream(slot.copy_stream):
    slot.maps_gpu.copy_(slot.host_tensors["map_tokens"], non_blocking=True)
    slot.action_features_gpu.copy_(slot.host_tensors["action_features"], non_blocking=True)
    slot.valid_gpu.copy_(slot.host_tensors["action_mask"], non_blocking=True)
    slot.copy_done.record(slot.copy_stream)

torch.cuda.current_stream().wait_event(slot.copy_done)
# The policy can now consume the GPU tensors. Reuse `slot` only after the
# transfer event is complete (or after a later event that also covers its use).
```

This API removes per-step output allocation and enables asynchronous H2D, but
it cannot by itself break the causal dependency `GPU chooses action -> CPU
simulates -> GPU evaluates next observation`. If simulation already keeps all
CPU cores busy while the GPU is at 50%, expect a modest gain from pinned memory
alone. Larger gains require independent work to overlap as well: for example,
two or more rollout slots, concurrent training on older rollout data, a larger
batch where memory permits, or batched MCTS with multiple pending leaves.

## Checked Actions And Independent Pipeline Slots

`step_checked(action_ids, state_ids)` and
`step_into_checked(action_ids, state_ids, buffers)` validate **all** state ids
under one native lock before advancing any game. If even one response is late,
the method raises and no row is changed. This prevents a delayed GPU result
from being applied to a newer position that happens to have a still-legal
action id.

For real CPU/GPU overlap, split one vector into 2--4 disjoint slots rather
than trying to advance the same games twice. `slot_partitions(n)` returns a
balanced partition of `env_id`s. Each slot has its own host/GPU buffers sized
by `slot_batch_spec(rows)`. While the GPU evaluates slot `N`, native code can
apply a completed action batch and encode the next observation for slot `N+1`.

```python
# `allocate()` creates exact NumPy arrays from a batch spec, as above.
slot_ids = env.slot_partitions(4)
slots = [
    {"env_ids": ids, "buffers": allocate(env.slot_batch_spec(len(ids)))}
    for ids in slot_ids
]

# Initial observations of all independent groups.
for slot in slots:
    env.observe_slots_into(slot["env_ids"], slot["buffers"])
    submit_to_gpu(slot)  # external code owns streams and CUDA events

# When one GPU result returns, use the ids from the position that produced it.
slot = next_slot_with_actions()
env.step_slots_into_checked(
    slot["env_ids"],
    slot["action_ids"],
    slot["buffers"]["state_id"],
    slot["buffers"],
)
submit_to_gpu(slot)
```

`observe_slots()` / `observe_slots_into()` do not advance games.
`step_slots_checked()` / `step_slots_into_checked()` reject duplicate or
out-of-range environment ids and atomically reject the entire slot on a stale
state id. `env_id` in the returned packet always identifies the authoritative
environment, even when its row position is local to a slot.

The engine deliberately does not own a CUDA stream, event, background Python
thread, or model callback. The external trainer must wait for the last GPU use
of a pinned host slot before handing that slot back to C++, and must wait for
the D2H action copy before calling a checked step. This keeps PolyEnv separate
from the model repository while making a safe producer/consumer schedule
possible.
