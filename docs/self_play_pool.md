# SelfPlayPool: Native Belief-MCTS Self-Play

`SelfPlayPool` is the high-throughput bridge between PolyEnv and an AI model
implemented in a separate repository. It owns many live games and their MCTS
trees in C++, while the external project owns the belief model, policy/value
model, GPU, optimizer, replay buffer, and training loop.

PolyEnv does **not** import PyTorch, CUDA, or a model callback. Its Python
boundary is a small number of dense NumPy arrays per phase.

```text
visible observation batch ──> external belief model
                                  │ completed map hypotheses
                                  ▼
live C++ games ──> detached belief roots ──> native MCTS forest
                                               │ dense leaf batch
external policy/value model <──────────────────┘
```

## Create A Pool

```python
from PolyEnv import Bardur, Imperius, SelfPlayPool

pool = SelfPlayPool(
    num_envs=256,
    seed=1234,
    map_size=11,
    players=(Bardur, Imperius),
    num_threads=8,
    max_actions=512,
    auto_reset=True,
    c_puct=1.5,
)
```

The current implementation supports exactly two players. Its MCTS backup uses
a scalar zero-sum value, just like `MctsPool`.

## Complete Lifecycle

The model code below is deliberately pseudocode: it can live in any external
repository and use any framework.

```python
import numpy as np

request = pool.reset()

while training:
    # Input contains only the current player's visible observation.
    completed_map_tokens = belief_model(request)
    # Must be a C-contiguous np.int32 array: [B, tiles, 23].

    pool.submit_beliefs(request["state_id"], completed_map_tokens)

    leaves = pool.select_leaves()
    for _ in range(search_rounds):
        if len(leaves["leaf_id"]) == 0:
            break

        policy_logits, values = policy_value_model(leaves)
        pool.expand_and_backup(
            leaves["leaf_id"],       # uint64, pass through unchanged
            policy_logits,            # float32 [L, max_actions]
            values,                   # float32 [L], from leaves["to_play"]
        )
        leaves = pool.select_leaves()

    root = pool.root_policy(temperature=1.0)
    rows = sample_rows(root["policy"], root["action_mask"])
    actions = root["action_id"][np.arange(pool.num_envs), rows]
    request = pool.step(actions)
```

`state_id` and `leaf_id` have different roles:

| Id | Dtype | Used for | Invalidated by |
| --- | --- | --- | --- |
| `state_id` | `uint64` | Matching a belief completion to a live game position | A valid real action or `reset()` |
| `leaf_id` | `uint64` | Matching policy/value output to an MCTS leaf | A backup, `step()`, or `reset()` |

Never reuse either id from an older batch. `submit_beliefs()` rejects stale
state ids. `step()` rejects an active pending leaf evaluation, rather than
letting a late GPU result modify a new position.

## Public Observation And Belief Input

`reset()`, `step()`, and `belief_requests()` return a dense batch with these
core model fields:

| Key | Dtype | Shape | Meaning |
| --- | --- | --- | --- |
| `map_tokens` | `int32` | `[B, tiles, 23]` | Current player's visible map only |
| `state` | `int32` | `[B, 11]` | Current player's game state |
| `action_id` | `int32` | `[B, Amax]` | Padded legal action ids |
| `action_features` | `int32` | `[B, Amax, 17]` | Action features |
| `action_arg_mask` | `uint8` | `[B, Amax, 12]` | Action argument validity |
| `action_mask` | `uint8` | `[B, Amax]` | Valid padded action rows |
| `legal_action_count` | `int32` | `[B]` | Number of legal actions |
| `env_id` / `to_play` | `int32` | `[B]` | Live slot and current player |
| `state_id` / `episode_id` | `uint64` | `[B]` | Opaque lifecycle ids |
| `reward`, `terminated`, `action_valid` | `float32`, `uint8`, `uint8` | `[B]` | Result of the preceding `step()` |

At reset, the final three fields are zero. If `auto_reset=True`, a terminal
game is immediately replaced by its next episode after the terminal result is
recorded in those fields.

`completed_map_tokens` submitted to `submit_beliefs()` has exact shape
`[B, tiles, 23]`, dtype `np.int32`, and C-contiguous layout. It must preserve:

- the observation's visibility mask in column `0`;
- every token in a currently visible row.

The external belief model may fill only hidden rows. PolyEnv validates the
complete result and rejects a hypothesis that changes the current player's
legal root action set. This prevents an imagined hidden object from producing
an action that cannot be executed in the live game.

## MCTS Leaf And Root Batches

After `submit_beliefs()`, `select_leaves()` returns the same core dense model
fields plus:

| Key | Dtype | Shape | Meaning |
| --- | --- | --- | --- |
| `leaf_id` | `uint64` | `[L]` | Handle passed to `expand_and_backup()` |
| `tree_id` / `env_id` | `int32` | `[L]` | MCTS tree and live game slot |
| `to_play` | `int32` | `[L]` | Perspective required for the value head |
| `state_id` / `episode_id` | `uint64` | `[L]` | Live root lifecycle metadata |

There is at most one pending leaf per tree. With `B` environments, a single
leaf batch has at most `B` rows; choose `num_envs` large enough to keep the
external GPU model efficiently batched.

`values` supplied to `expand_and_backup()` must be finite `float32` values in
`[-1, 1]`, from the perspective of `leaves["to_play"]`. `policy_logits` has
shape `[L, max_actions]` and is indexed by padded **action row**, not global
action id. Apply `action_mask` in the external model.

`root_policy()` returns `action_id`, `action_mask`, `visit_count`, `policy`,
`root_value`, `root_visit_count`, and `root_player`, one row per live game. It
also adds `env_id`, `state_id`, and `episode_id`.

## Fog Of War And Information Safety

The live game remains authoritative inside C++ so it can execute rules. It is
never used as an MCTS root after `submit_beliefs()` and its full map is never
returned by `SelfPlayPool`.

For each slot, PolyEnv builds a new detached belief world directly from the
submitted tensor. It verifies visible rows before constructing hidden cities,
units, and terrain from the hypothesis. The model leaf packet is then encoded
through the current player view; the completed hidden rows do not appear in
`leaves["map_tokens"]`.

This is **root-perspective determinized MCTS**, not full ISMCTS. In particular,
the current belief format contains a map completion, while an exact opponent
information state would also require predicted stars, technologies, city/unit
runtime state, per-player visibility, and redeterminization when the actor
changes. PolyEnv intentionally does not copy those hidden opponent facts from
the live game. That preserves the no-leak boundary, but means opponent nodes
are an approximation until a future explicit multi-player belief-state format
is added.

Use `GameEnv.full_map_numpy()` only for offline labels, tests, or debugging;
do not feed it to `SelfPlayPool` during fog-of-war self-play.

## Performance Characteristics

- Live simulation, belief validation/building, PUCT selection, lazy branch
  creation, and backup are native C++ work with the GIL released.
- One persistent C++ worker pool is shared between live-game work and MCTS;
  they are scheduled in separate phases, avoiding nested CPU thread pools.
- A belief batch is consumed as one contiguous `int32` tensor. It is not
  converted into Python objects or a nested vector for every game.
- `MctsPool` branch creation stays lazy: only PUCT-selected children clone a
  game state, so memory grows with visited nodes rather than all legal moves.
- Returned arrays are fresh NumPy arrays. PolyEnv makes no claim of CPU-to-GPU
  zero copy: an external PyTorch/JAX/etc. repository should choose pinned host
  memory and asynchronous transfers if those are beneficial on its hardware.

Larger `num_envs` usually improves GPU throughput but increases live-state,
belief-root, and MCTS-tree memory. More CPU threads improve independent game
and leaf work until they contend with the model process; profile the complete
training job rather than maximizing `num_threads` blindly.

`SelfPlayPool` intentionally does not currently expose
`visible_event_history`: MCTS branch sessions omit replay/event journals for
memory efficiency, so offering the field only on the belief request would make
the model interface inconsistent between root and leaf batches.
