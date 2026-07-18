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

`SelfPlayPool` supports **2--16 players**. Two-player games retain the compact,
optimized scalar zero-sum MCTS path. With more players it uses Maxⁿ MCTS: the
policy at each node maximizes the payoff component of that node's current
actor. All environments in one pool therefore have the same fixed player
count, exposed as `pool.player_count` (alias: `pool.num_players`).

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
            values,                   # 2P: float32 [L], from leaves["to_play"]
                                     # P>2: float32 [L, P], global PlayerId order
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
| `legal_action_count` | `int32` | `[B]` | Number of represented action rows (`<= Amax`) |
| `total_legal_action_count` | `int32` | `[B]` | Legal actions before fixed-capacity truncation |
| `action_truncated` | `uint8` | `[B]` | `1` when not every legal action fit in the batch |
| `env_id` / `to_play` | `int32` | `[B]` | Live slot and current player |
| `state_id` / `episode_id` | `uint64` | `[B]` | Opaque lifecycle ids |
| `reward`, `terminated`, `action_valid` | `float32`, `uint8`, `uint8` | `[B]` | Result for the actor of the preceding `step()` |
| `winner` | `int32` | `[B]` | Terminal winner of the preceding step, or `-1` for no winner/no terminal transition |
| `terminal_values` | `float32` | `[B, P]` | Terminal payoff vector for the preceding step; zero otherwise |

At reset, `reward`, `terminated`, `action_valid`, and `terminal_values` are
zero and `winner` is `-1`. If `auto_reset=True`, a terminal game is immediately
replaced by its next episode after the terminal result is recorded in those
fields. Preserve that transition result before consuming the returned next
observation.

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
| `to_play` | `int32` | `[L]` | Actor at the selected MCTS leaf |
| `player_count` | `int32` | `[L]` | Fixed `P`; determines the MCTS value tensor shape |
| `total_legal_action_count` | `int32` | `[L]` | Leaf legal-action count before truncation |
| `action_truncated` | `uint8` | `[L]` | `1` when the leaf action set exceeded `max_actions` |
| `state_id` / `episode_id` | `uint64` | `[L]` | Live root lifecycle metadata |

There is at most one pending leaf per tree. With `B` environments, a single
leaf batch has at most `B` rows; choose `num_envs` large enough to keep the
external GPU model efficiently batched.

`values` supplied to `expand_and_backup()` must be finite `float32` values in
`[-1, 1]`. For `pool.player_count == 2`, pass scalar shape `[L]` from the
perspective of `leaves["to_play"]`. For `P > 2`, pass exact shape `[L, P]`:
column `p` is the payoff for global `PlayerId == p`, not a value rotated to the
leaf actor. `policy_logits` has shape `[L, max_actions]` and is indexed by
padded **action row**, not global action id. Apply `action_mask` in the
external model.

When a legal set exceeds `max_actions`, PolyEnv deterministically truncates it
to the canonical prefix and reserves the final row for `EndTurn` when needed.
It does not abort the self-play loop. Watch `action_truncated` and
`total_legal_action_count`: a truncated MCTS tree cannot evaluate omitted
moves, so choose a capacity that makes this exceptional rather than routine.

`root_policy()` returns `action_id`, `action_mask`, `visit_count`, `policy`,
`root_value`, `root_visit_count`, `root_player`, and `player_count`, one row
per live game. `root_value` is always the component for `root_player`, keeping
the old scalar output shape `[B]`. It also adds `env_id`, `state_id`, and
`episode_id`.

For a resolved terminal transition, `terminal_values` gives the complete
training target: winner `+1`, each loser `-1 / (P - 1)`, and all zero for a
terminal state with no winner. This is exactly the historical `+1/-1` result
for two players.

## Reusable Pinned Batches

The ordinary methods return fresh NumPy arrays for convenience. For the hot
self-play path, use caller-owned arrays instead. `belief_batch_spec()`
describes the fixed `[B, ...]` request packet accepted by all of:

```python
pool.reset_into(belief_buffers, seed=1234)
pool.belief_requests_into(belief_buffers)
pool.step_into(action_ids, belief_buffers)
```

The leaf batch is dynamic, so it uses a fixed-capacity buffer and returns its
valid prefix length:

```python
leaf_buffers = allocate(pool.leaf_batch_spec(max_leaves=128))
root_buffers = allocate(pool.root_policy_spec())

leaf_count = pool.select_leaves_into(leaf_buffers, max_leaves=128)
if leaf_count:
    policy_logits, values = policy_value_model_prefix(leaf_buffers, leaf_count)
    pool.expand_and_backup(
        leaf_buffers["leaf_id"][:leaf_count],
        policy_logits,
        values,
    )

pool.root_policy_into(root_buffers, temperature=1.0)
```

Here `allocate(spec)` creates one exact NumPy array for every entry in a spec;
the generic implementation is shown in
[MctsPool's reusable-buffer section](mcts_pool.md#reusable-pinned-leaf-buffers).
`leaf_batch_spec(max_leaves=C)` and `select_leaves_into(..., max_leaves=C)`
must use the same capacity. Only rows `[0:leaf_count]` are valid; the tail is
not cleared. This includes `env_id`, `state_id`, and `episode_id`, so a model
or scheduler must slice all leaf fields consistently.

Every output array is validated before live state or MCTS pending state
changes: it must have the exact dtype and shape from its spec, be writable,
aligned and C-contiguous, and not overlap any other batch field. `step_into()`
accepts a one-dimensional `int32` action array and copies that small vector
before overwriting the output slot, so `belief_buffers["action_id"][:, 0]` is
safe to use as input.

PolyEnv stays framework-agnostic. An external PyTorch trainer can make the
large model-input fields NumPy views of `torch.empty(..., pin_memory=True)`,
then enqueue H2D copies with `non_blocking=True`. It must retain the tensor
owners and not overwrite a slot until the CUDA event covering its last GPU read
has completed. Conversely, wait for asynchronous GPU-to-CPU logits, values,
beliefs, or actions before PolyEnv reads them. See the full double-buffering
example in [VectorGameEnv](vector_env.md#reusable-pinned-buffers-and-asynchronous-transfer).

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
the live game. That preserves the no-leak boundary, but makes opponent nodes
an approximation; the impact becomes greater with more players.

### Current belief-state limits

The multi-player search API is functional, but the current fog-of-war belief
format is not yet a faithful multi-player information-state model:

- `completed_map_tokens` represents one root player's map. The belief builder
  installs that player's visibility only. After a turn change, a non-root
  actor can have an under-complete legal-action set (in an extreme case, only
  `EndTurn`). Supplying the real opponent visibility would fix mechanics but
  would leak hidden knowledge, so the correct future solution is explicit
  per-player belief state plus redeterminization.
- The tensor does not carry pending city-upgrade choices or all player runtime
  state. A root position with such a pending choice can fail the root legal
  action equivalence check until the belief-state contract is extended.
- `submit_beliefs()` validates visible rows and the current root's legal
  actions; it cannot prove that the hypothesis creates realistic future
  opponent economies or action sets.

These are search-quality limitations, not a path for reading the live hidden
world. They should be addressed before treating multi-player fog self-play as
a fully faithful ISMCTS implementation.

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
- The two-player search retains its scalar/sign-flip backup. Multi-player
  Maxⁿ consumes value vectors but keeps only the actor's scalar statistic at
  each node and edge, avoiding per-edge `P`-vector allocations.
- The ordinary methods return fresh NumPy arrays. The `*_into()` methods avoid
  those allocations and can write into framework-owned pinned host memory, but
  GPU streams, events, and the physical H2D/D2H transfer remain the external
  repository's responsibility.

Larger `num_envs` usually improves GPU throughput but increases live-state,
belief-root, and MCTS-tree memory. More CPU threads improve independent game
and leaf work until they contend with the model process; profile the complete
training job rather than maximizing `num_threads` blindly.

`SelfPlayPool` intentionally does not currently expose
`visible_event_history`: MCTS branch sessions omit replay/event journals for
memory efficiency, so offering the field only on the belief request would make
the model interface inconsistent between root and leaf batches.
