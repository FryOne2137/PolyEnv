# MctsPool: Native Batched PUCT

`MctsPool` runs many independent MCTS trees in native C++. It performs PUCT
selection, state branching, expansion, and value backup without a Python call
per node. Python is responsible only for one batched policy/value inference on
the selected leaves, which is the natural GPU boundary.

This is distinct from `VectorGameEnv`: a vector environment owns independent
episodes, while an MCTS pool owns independent *search trees* rooted at game
snapshots.

When one external project needs both many live self-play games and belief-root
MCTS, prefer [`SelfPlayPool`](self_play_pool.md). It keeps the hand-off between
live positions, checked belief completions, and MCTS trees in C++.

## Create A Pool

Pass a sequence of root positions, one per independent search, or one root
with `num_trees` to make isolated native copies:

```python
from PolyEnv import Bardur, GameEnv, Imperius, MctsPool

root = GameEnv(seed=1234, players=(Bardur, Imperius))
pool = MctsPool(
    root,
    num_trees=256,
    num_threads=8,
    max_actions=512,
    c_puct=1.5,
    # Start at 1 for legacy behavior. 2--8 permits larger GPU batches.
    max_pending_leaves_per_tree=4,
    virtual_loss=1.0,
    # 0 disables a limit. Use at least one of these for long searches.
    max_nodes_per_tree=20_000,
    max_tree_bytes=512 * 1024 * 1024,
)
```

The source `GameEnv` is never mutated. Every root must have the same map size
and number of players. Pools support **2--16 players**; the engine's visibility
representation cannot safely support more than 16.

`pool.player_count` (alias: `pool.num_players`) returns this fixed count.

For two players, `MctsPool` uses its optimized, backward-compatible zero-sum
path. For three or more players it uses **Maxⁿ PUCT**: at each node, the actor
selects the action that maximizes that actor's own value component. This is not
paranoid MCTS: opponents do not implicitly minimize the root player's score.

## Search Loop

First expand the roots, then repeat one model batch and one native backup.

- With two players, the network returns scalar `value[L]` in `[-1, 1]` from
  the perspective identified by `leaves["to_play"]`.
- With `P > 2`, the network returns `values[L, P]` in `[-1, 1]`. Column `p`
  is the payoff for global `PlayerId == p`, independent of the leaf actor.
  Do not broadcast a scalar or rotate columns without applying the same
  permutation to player ids.

```python
import numpy as np
import torch

leaves = pool.select_leaves()       # up to max_pending_leaves_per_tree per tree

for _ in range(128):
    if len(leaves["leaf_id"]) == 0:
        break                        # all trees are terminal or waiting

    maps = torch.from_numpy(leaves["map_tokens"]).to("cuda")
    actions = torch.from_numpy(leaves["action_features"]).to("cuda")
    action_mask = torch.from_numpy(leaves["action_mask"]).to("cuda", dtype=torch.bool)

    logits, value = model(maps, actions, action_mask)
    pool.expand_and_backup(
        leaves["leaf_id"],
        logits.detach().float().cpu().numpy(),       # [L, max_actions]
        value.detach().float().squeeze(-1).cpu().numpy(),  # 2P: [L], in [-1, 1]
    )
    leaves = pool.select_leaves()

root = pool.root_policy(temperature=1.0)
```

`select_leaves()` returns the core dense model inputs used by `VectorGameEnv`:
`map_tokens`, `state`, padded `action_id`, `action_features`,
`action_arg_mask`, `action_mask`, and `legal_action_count`. It also returns:

| Key | Dtype | Shape | Meaning |
| --- | --- | --- | --- |
| `leaf_id` | `uint64` | `[L]` | Opaque handle passed unchanged to `expand_and_backup()` |
| `tree_id` | `int32` | `[L]` | Index of the independent search tree |
| `to_play` | `int32` | `[L]` | Actor at the selected leaf |
| `player_count` | `int32` | `[L]` | Fixed `P` for the pool; determines the value tensor shape |
| `total_legal_action_count` | `int32` | `[L]` | Leaf legal-action count before truncation |
| `action_truncated` | `uint8` | `[L]` | `1` when the leaf action set exceeded `max_actions` |

`L <= num_trees * max_pending_leaves_per_tree`. The default is one outstanding
neural evaluation per tree, preserving the former race-free behavior. With a
setting of `2` through `8`, the pool applies native virtual loss along each
reserved path and can return several different leaves from one tree. Pending
work is never counted in `root_policy()`; its visits and values include only
completed backups. The virtual loss is removed exactly once before backup or
when a leaf is cancelled.

The first selection of an unexpanded root still supplies one leaf: the policy
is needed before the scheduler can fan out over that root's actions. Later
calls can fill the wider per-tree capacity.

The supplied logits correspond to padded action **rows**, not global action
ids. They must have exact shape `[L, max_actions]`; only valid rows are read
and their masked softmax becomes the PUCT prior. `-inf` is allowed for a legal
row, but if all legal logits are `-inf`, the pool falls back to uniform priors.

## Root Result

`root_policy()` returns one padded action distribution per tree:

| Key | Dtype | Shape | Meaning |
| --- | --- | --- | --- |
| `action_id` | `int32` | `[T, Amax]` | Root legal action ids |
| `action_mask` | `uint8` | `[T, Amax]` | Valid root rows |
| `visit_count` | `int32` | `[T, Amax]` | PUCT edge visits |
| `policy` | `float32` | `[T, Amax]` | Visit policy at the requested temperature |
| `root_value` | `float32` | `[T]` | Mean value from the root player's perspective |
| `root_visit_count` | `int32` | `[T]` | Number of completed evaluations/backups |
| `root_player` | `int32` | `[T]` | Player whose component is returned in `root_value` |
| `player_count` | `int32` | `[T]` | Fixed number of players in the pool |
| `total_legal_action_count` | `int32` | `[T]` | Root legal-action count before truncation |
| `action_truncated` | `uint8` | `[T]` | `1` when the root action set exceeded `max_actions` |

Choose an id from `root["action_id"]`, then execute that id on the original
`GameEnv`. After the real game advances, create a new pool from the new
snapshot. This keeps the public game state and all search-state ownership
unambiguous.

`temperature=0.0` returns a deterministic one-hot policy: highest visit count,
then highest prior, then lowest action id on ties.

## Reusable Pinned Leaf Buffers

The allocating methods above are simplest for inspection and small searches.
For a long-running GPU search loop, preallocate caller-owned arrays instead:

```python
import numpy as np

def allocate(spec):
    return {
        name: np.empty(tuple(field["shape"]), dtype=field["dtype"])
        for name, field in spec.items()
    }

# The capacity C must match max_leaves passed to select_leaves_into().
leaf_buffers = allocate(pool.leaf_batch_spec(max_leaves=128))
root_buffers = allocate(pool.root_policy_spec())

leaf_count = pool.select_leaves_into(leaf_buffers, max_leaves=128)
if leaf_count:
    # Only this prefix is current. Rows [leaf_count:C] are intentionally stale.
    pool.expand_and_backup(
        leaf_buffers["leaf_id"][:leaf_count],
        policy_logits[:leaf_count],
        values[:leaf_count],
    )

pool.root_policy_into(root_buffers, temperature=1.0)
```

`leaf_batch_spec()` exposes a capacity of
`num_trees * max_pending_leaves_per_tree`; calling it with `max_leaves=C`
exposes capacity `min(C, num_trees * max_pending_leaves_per_tree)`. The same `max_leaves`
must be used by `select_leaves_into()`. Its integer result is the only valid
row count: terminal, budget-limited or already-pending trees can make it smaller than the
capacity, including zero. `root_policy_into()` is fixed at `num_trees` rows.

All `*_into()` arrays need the exact dtype and shape from their spec, must be
writable, aligned and C-contiguous, and may not overlap. Validation happens
before leaf selection, so a malformed buffer cannot leave a tree pending.
The engine holds strong references only during the synchronous call and never
retains an output pointer afterward.

The arrays may be NumPy views of pinned PyTorch host tensors. Pin the actual
model inputs (`map_tokens`, `state`, action features and masks), transfer them
on a copy stream with `non_blocking=True`, and keep at least two buffer slots
if independent tree groups allow useful overlap. The allocator and CUDA-event
protocol are shown in [VectorGameEnv's pinned-buffer guide](vector_env.md#reusable-pinned-buffers-and-asynchronous-transfer).
Before `expand_and_backup()`, wait for any asynchronous GPU-to-CPU copy and
pass C-contiguous `uint64` leaf ids plus `float32` logits/values to avoid an
implicit conversion.

## Cancellation, Budgets And Observability

An evaluator error or a discarded asynchronous batch must not leave a tree
permanently pending. Use `cancel_leaves()` for a known batch, or
`abort_search()` to invalidate every outstanding leaf while retaining all
completed branches and backups:

```python
try:
    pool.expand_and_backup(leaves["leaf_id"], logits, values)
except Exception:
    pool.cancel_leaves(leaves["leaf_id"])  # idempotent for already-finished ids
    raise

# At a shutdown boundary or after abandoning all in-flight GPU requests:
pool.abort_search()
```

`configure_search(max_pending_leaves_per_tree, virtual_loss,
max_nodes_per_tree, max_tree_bytes)` changes the scheduler only when no leaves
are pending. `max_pending_leaves_per_tree` is deliberately capped at `8`;
the useful range is usually `2--8`. `max_nodes_per_tree=0` and
`max_tree_bytes=0` disable their respective limits.

The node limit is exact. The byte limit is a hard **admission budget** for
native MCTS allocations: it accounts for node/edge storage exactly and
reserves a conservative map/player-dependent amount before cloning a game
branch. It is intentionally not advertised as an exact process-RSS meter,
because `GameEnv` and the standard library own allocator-dependent internal
storage. A leaf whose edge vector would not fit is never sent to the model;
an unmaterialized child that would not fit is skipped. Inspect
`pool.search_stats()` for `tree_node_count`, `tree_accounted_bytes`, pending
counts and budget-exhaustion flags.

## Performance Design

- Tree control and PUCT backup run in C++ with the GIL released.
- Expansion is **lazy**: priors are stored for every legal action, but an
  independent `GameEnv` branch is cloned only when PUCT first selects its edge.
  Wide action spaces therefore use memory proportional to visited nodes rather
  than every possible child.
- Search branches drop replay and visible-event journal history while retaining
  the authoritative game and fog-of-war observation knowledge needed to encode
  model inputs.
- `num_threads` parallelizes independent tree branches during encoding and
  expansion. Do not wrap pool calls in a Python thread pool.
- The two-player path stores and backs up one scalar with a sign flip. The
  multi-player Maxⁿ path still stores only one scalar per node and edge: the
  component for that node's actor. It avoids allocating a `P`-element vector
  for every visited edge.
- `max_actions` is a fixed action-window capacity. Wide legal sets are
  deterministically truncated to the canonical prefix, while retaining
  `EndTurn` in the final row when needed. Leaf batches expose
  `total_legal_action_count` and `action_truncated`; make truncation rare for
  stronger search.

At a terminal state the winner receives `+1`, every loser receives
`-1 / (P - 1)`, and a terminal state with no resolved winner receives zero.
This is `+1/-1` for two players and keeps the multi-player target zero-sum.

## Fog Of War

For perfect-information search, pass `env` or `env.clone()`. For a legal
fog-of-war hypothesis, first create a detached belief world, then pass that
world to `MctsPool`:

```python
belief = env.make_belief_env(completed_map_tokens)
pool = MctsPool(belief, num_trees=128)
```

Do not pass the original hidden-state `GameEnv` when the search policy must not
access hidden source-world information. See [Hidden-Map Predictions](hidden_map_predictions.md).
