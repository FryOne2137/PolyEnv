# Replays

PolyEnv records an action replay, not a full state snapshot. The replay is a
JSON file conventionally named with the `.polygame` extension.

## Python

```python
from PolyEnv import GameEnv, Bardur, Imperius

env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))

action_id = int(env.legal_action_ids_fast()[0])
env.step_fast(action_id)

env.save("match.polygame")

replayed = GameEnv()
final_observation = replayed.load("match.polygame")
```

Every successful `step`, `step_fast`, `step_fast_no_reveal`, `step_param`, and
`step_param_vec` contributes its resolved `action_id` to the native C++ replay
recorder. Invalid actions are not recorded.

Use `env.replay_action_ids()` when you need to inspect the recorded ids without
writing a file.

## File Contents

```json
{
  "format": "polyenv-game",
  "format_version": 2,
  "engine_version": "0.2.0",
  "ruleset": "polyenv-2026-07",
  "seed": 1234,
  "map_size": 11,
  "tribes": [3, 2],
  "map_generation": {
    "initial_land": 0.5,
    "smoothing": 3,
    "relief": 4
  },
  "actions": [0, 45123, 9]
}
```

`seed` is the effective seed. Therefore a game started with `seed=0` still
writes a concrete non-zero seed that can be replayed later.

## Replay Semantics

Loading reconstructs the map from the saved metadata and applies every action
in order. The loader rejects a malformed header and stops if an action is not
legal in the reconstructed state.

`save_state()` and `load_state()` are different: they are in-memory snapshots
for branching and MCTS. They are not portable files.

`clone()` and `copy()` preserve the native replay history. The core `Game`
state itself does not store that history, so MCTS clones of `Game` do not carry
an unnecessary growing action vector.

## Compatibility

Replays are designed to be reproducible with the same PolyEnv ruleset. Format
`v2` records that ruleset explicitly and the loader rejects a mismatch before
trying to execute any action. Format `v1` files are legacy files: they predate
the ruleset marker and cannot be replayed after incompatible map-generation or
action-space changes.

Keep the `.polygame` file together with the PolyEnv version used to create it.
Cross-platform bit-for-bit determinism is not guaranteed across different C++
standard-library implementations or engine releases.
