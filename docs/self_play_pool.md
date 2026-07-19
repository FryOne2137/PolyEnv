# SelfPlayPool: Native Belief-MCTS Self-Play

`SelfPlayPool` is the high-throughput bridge between PolyEnv and an AI model
implemented in a separate repository. It owns many live games and their MCTS
trees in C++, while the external project owns the belief model, policy/value
model, GPU, optimizer, replay buffer, and training loop.

PolyEnv does **not** import PyTorch, CUDA, or a model callback. Its Python
boundary is a small number of dense NumPy arrays per phase.

```text
per-player observations ──> external belief model
                                │ per-player belief particles
                                ▼
live C++ games ──> detached ISMCTS belief forest ──> native MCTS
                                                       │ dense leaf batch
external policy/value model <────────────────────────┘
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
    # 0: complete stable ActionSpace; no legal move is omitted.
    max_actions=0,
    require_all_actions=True,
    auto_reset=True,
    c_puct=1.5,
    visible_action_history=512,  # 0--1024 compact visible-action tokens
    max_pending_leaves_per_tree=4,  # 1--8; 1 preserves legacy behavior
    virtual_loss=1.0,
    max_nodes_per_tree=20_000,      # 0 disables this hard admission limit
    max_tree_bytes=512 * 1024 * 1024,
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
    # One fog-safe observation for every player. The belief model must never
    # receive one player's row as input for another player.
    belief_request = pool.all_player_belief_requests()
    completed_map_tokens = belief_model(belief_request)
    # C-contiguous int32 [B, P, K, tiles, 23]. K >= 1 belief particles.
    # [B, P, tiles, 23] is accepted as the K=1 shorthand.

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
    request = pool.step_checked(actions, root["state_id"])
```

`state_id` and `leaf_id` have different roles:

| Id | Dtype | Used for | Invalidated by |
| --- | --- | --- | --- |
| `state_id` | `uint64` | Matching a belief completion to a live game position | A valid real action or `reset()` |
| `leaf_id` | `uint64` | Matching policy/value output to an MCTS leaf | A backup, `step()`, or `reset()` |

Never reuse either id from an older batch. `submit_beliefs()` rejects stale
state ids. For an asynchronous policy result, use
`step_checked(actions, root["state_id"])` or `step_into_checked(...)`: it
validates every live slot before advancing any of them. The legacy `step()`
still rejects an active pending leaf evaluation, but cannot distinguish a late
action that remains legal in a newer position.

Calling `submit_beliefs()` with `[B, tiles, 23]` retains the legacy
root-perspective determinization mode. Use the per-player form above for
ISMCTS; it is the intended training path whenever fog of war matters.

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
| `legal_action_count` | `int32` | `[B]` | Number of legal action rows represented for the model |
| `total_legal_action_count` | `int32` | `[B]` | Total engine-generated legal actions |
| `action_truncated` | `uint8` | `[B]` | Always `0` when `require_all_actions=True` |
| `env_id` / `to_play` | `int32` | `[B]` | Live slot and current player |
| `state_id` / `episode_id` | `uint64` | `[B]` | Opaque lifecycle ids |
| `visible_action_history` | `int32` | `[B, H, 12]` | Right-aligned compact history observed by `to_play` |
| `visible_action_history_mask` | `uint8` | `[B, H]` | `1` for a valid history row |
| `visible_action_history_length` | `int32` | `[B]` | Valid row count, capped at `H` |
| `reward`, `terminated`, `action_valid` | `float32`, `uint8`, `uint8` | `[B]` | Result for the actor of the preceding `step()` |
| `winner` | `int32` | `[B]` | Terminal winner of the preceding step, or `-1` for no winner/no terminal transition |
| `terminal_values` | `float32` | `[B, P]` | Terminal payoff vector for the preceding step; zero otherwise |

At reset, `reward`, `terminated`, `action_valid`, and `terminal_values` are
zero and `winner` is `-1`. If `auto_reset=True`, a terminal game is immediately
replaced by its next episode after the terminal result is recorded in those
fields. Preserve that transition result before consuming the returned next
observation.

The action rows come directly from `GameStateAdapter`'s canonical legal-action
enumerator. They therefore include every **currently legal** engine action:
`EndTurn`, `BuyTech`, movement, attack, heal, city upgrades, buildings, unit
spawns, roads/bridges and every other tile/unit action. `SelfPlayPool` does not
maintain a reduced action vocabulary. With the default
`require_all_actions=True`, a capacity overflow raises instead of silently
omitting moves. Set `max_actions=0` to derive `pool.action_space_size`, which
guarantees capacity for the complete stable action space. Setting
`require_all_actions=False` is an explicit legacy/performance trade-off and
restores deterministic truncation plus `action_truncated=1`.

### Per-player ISMCTS belief input

`all_player_belief_requests()` returns a fog-safe packet with map/state/history
shapes `[B, P, ...]`, plus `belief_player[B, P]` and the repeated live
`to_play[B, P]`. It contains no action rows:
only `to_play` chooses an action in the live game, while MCTS leaf packets
always contain the complete legal action list for the leaf actor. The external
belief model expands this packet to either:

```python
# One particle per player (valid but less diverse)
completed.shape == (B, P, tiles, 23)

# Recommended: K independent completions per player's own observation.
completed.shape == (B, P, K, tiles, 23)
```

Every visible row in every particle is validated against that same player's
observation. The current actor's particles additionally must reproduce the
live legal action set. A malformed particle rejects the entire submission, so
the active MCTS forest is never half-updated.

For pinned, reusable host storage use
`all_player_belief_batch_spec()` with
`all_player_belief_requests_into(buffers)`. This packet is emitted once per
real move, before `submit_beliefs()`, and can share the same double-buffering
discipline as the ordinary belief packet.

`completed_map_tokens` submitted to `submit_beliefs()` has exact shape
`[B, tiles, 23]`, dtype `np.int32`, and C-contiguous layout. It must preserve:

- the observation's visibility mask in column `0`;
- every token in a currently visible row.

The external belief model may fill only hidden rows. PolyEnv validates the
complete result and rejects a hypothesis that changes the current player's
legal root action set. This prevents an imagined hidden object from producing
an action that cannot be executed in the live game.

### Compact visible-action history

`visible_action_history=H` enables a fixed window of `0..1024` earlier
**observed world actions**. The newest token is at `H - 1`; left padding has a
zero mask. The same fields occur in belief and MCTS leaf batches, so the model
receives the history appropriate for the actor of each hypothetical leaf.

Each row has 12 `int32` features: `event_type`, `actor_player`, `actor_tribe`,
`visibility_flags`, `source_index`, `target_index`, `source_unit_type`,
`target_unit_type`, `source_observed_unit_id`, `target_observed_unit_id`,
`detail_kind`, and `detail_value`.

This is not a replay of raw action ids. Source/target and unit fields have
already been filtered when the action occurred; an unseen opponent move adds
no row for that observer. `EndTurn`, technology purchases, and other
non-world events also add no row. This prevents fog-of-war leaks.

The pool stores immutable compact entries and shares their prefixes between
MCTS nodes; it packs `H` rows only for selected batch entries. At `H=1024`,
the channel is 48 KiB per row: about 12 MiB for 256 belief rows or 48 MiB for
1024 leaf rows. Use `*_into()` and externally pinned buffers for large runs.

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
| `visible_action_history` | `int32` | `[L, H, 12]` | History observed by this leaf's `to_play` actor |
| `visible_action_history_mask` / `visible_action_history_length` | `uint8`, `int32` | `[L, H]`, `[L]` | Valid rows and their count |

The default has one pending leaf per tree. Set
`max_pending_leaves_per_tree` to `2--8` to obtain up to `B * pending` rows per
batch after roots have expanded. Native virtual loss reserves each in-flight
path, avoids duplicate work, and is rolled back before backup or cancellation.
The first unexpanded root still produces one leaf because its policy must be
known before the tree can fan out.

`values` supplied to `expand_and_backup()` must be finite `float32` values in
`[-1, 1]`. For `pool.player_count == 2`, pass scalar shape `[L]` from the
perspective of `leaves["to_play"]`. For `P > 2`, pass exact shape `[L, P]`:
column `p` is the payoff for global `PlayerId == p`, not a value rotated to the
leaf actor. `policy_logits` has shape `[L, max_actions]` and is indexed by
padded **action row**, not global action id. Apply `action_mask` in the
external model.

For exact action coverage keep `require_all_actions=True` (the default). A
leaf or root whose legal count exceeds `max_actions` then fails before a model
sees a reduced policy target. The opt-out truncation mode reserves the final
row for `EndTurn` when needed, but a truncated tree cannot evaluate omitted
moves and is unsuitable for exact training.

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
pool.step_into_checked(action_ids, belief_buffers["state_id"], belief_buffers)
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
must use the same capacity. Without `max_leaves`, capacity is
`B * max_pending_leaves_per_tree`; only rows `[0:leaf_count]` are valid and
the tail is not cleared. This includes `env_id`, `state_id`, and `episode_id`,
so a model or scheduler must slice all leaf fields consistently.

Every output array is validated before live state or MCTS pending state
changes: it must have the exact dtype and shape from its spec, be writable,
aligned and C-contiguous, and not overlap any other batch field.
`step_into_checked()` accepts one-dimensional `int32` actions plus matching
`uint64` state ids and copies both small vectors before overwriting the output
slot, so `belief_buffers["action_id"][:, 0]` and
`belief_buffers["state_id"]` are safe inputs. `step_into()` remains available
for synchronous legacy loops.

If an evaluator times out or a slot is discarded, call `cancel_leaves(ids)`;
at shutdown or when dropping a whole batch call `abort_search()`. Both retain
completed tree work and make late leaf ids stale. `search_stats()` exposes
per-tree node count, accounted byte budget, pending count and budget-exhausted
flags. See [MctsPool](mcts_pool.md#cancellation-budgets-and-observability) for
the exact admission-budget semantics.

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

The detached world uses a deterministic synthetic RNG seed derived only from
public position metadata and that submitted hypothesis. The authoritative
world seed is never copied into a belief root, so seed-based hidden outcomes
cannot leak through MCTS rollouts.

The `[B, P, K, ...]` contract enables **per-player re-determinizing ISMCTS**.
At an actor change, C++ selects a particle belonging to the next actor from a
stable branch hash, rebuilds a detached world from that actor's own belief,
replays their own actions and public `EndTurn` transitions, then merges only
cells visible in the already-simulated belief world. Thus a player never acts
from the predecessor's hidden completion, and the authoritative world is never
read by the tree after initial per-player validation.

The quality of ISMCTS is bounded by the external belief distribution: `K=1`
is a valid deterministic special case but does not represent uncertainty well;
use several diverse, internally consistent particles when GPU budget permits.
The engine validates visibility and active-root legality, but cannot prove that
an externally hallucinated hidden economy is statistically realistic. That is
a model-quality issue, not a fog-of-war leak.

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
  repository's responsibility. `all_player_belief_requests()` is intentionally
  a separate per-player packet: it is emitted once per real move, not per MCTS
  leaf; its cost is `O(B * P * tiles)` rather than `O(nodes * P)`.

Larger `num_envs` usually improves GPU throughput but increases live-state,
belief-root, and MCTS-tree memory. More CPU threads improve independent game
and leaf work until they contend with the model process; profile the complete
training job rather than maximizing `num_threads` blindly.

`SelfPlayPool` intentionally does not expose VectorGameEnv's large
`visible_event_history` layout. It would be too costly to duplicate across
MCTS leaves. Use the compact, fog-safe `visible_action_history` channel,
which is available consistently on both belief and leaf packets.
