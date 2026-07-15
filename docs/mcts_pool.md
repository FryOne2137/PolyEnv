# MctsPool: Native Batched PUCT

`MctsPool` runs many independent two-player MCTS trees in native C++. It
performs PUCT selection, state branching, expansion, and value backup without
a Python call per node. Python is responsible only for one batched policy/value
inference on the selected leaves, which is the natural GPU boundary.

This is distinct from `VectorGameEnv`: a vector environment owns independent
episodes, while an MCTS pool owns independent *search trees* rooted at game
snapshots.

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
)
```

The source `GameEnv` is never mutated. Every root must be a two-player game
with the same map size. `MctsPool` is intentionally restricted to two players:
its scalar value and PUCT backup use zero-sum sign changes. A multi-player
version needs a vector-valued evaluator and a different selection objective.

## Search Loop

First expand the roots, then repeat one model batch and one native backup. The
network's scalar `value` must be in `[-1, 1]` **from the perspective identified
by `leaves["to_play"]`**.

```python
import numpy as np
import torch

leaves = pool.select_leaves()       # at most one leaf per tree

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
        value.detach().float().squeeze(-1).cpu().numpy(),  # [L], in [-1, 1]
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
| `to_play` | `int32` | `[L]` | Player perspective required for the value head |

`L <= num_trees`: a tree has at most one outstanding neural evaluation.
Calling `select_leaves()` again before backing up a leaf returns no duplicate
for that tree. This makes every GPU batch race-free without Python-side
virtual-loss bookkeeping.

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

Choose an id from `root["action_id"]`, then execute that id on the original
`GameEnv`. After the real game advances, create a new pool from the new
snapshot. This keeps the public game state and all search-state ownership
unambiguous.

`temperature=0.0` returns a deterministic one-hot policy: highest visit count,
then highest prior, then lowest action id on ties.

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
- `max_actions` is capacity, not truncation. The pool raises an error if a
  selected leaf exceeds it.

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
