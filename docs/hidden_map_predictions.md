# Hidden-Map Predictions

Use `GameEnv.make_belief_env()` for MCTS under fog of war. It creates a new,
detached hypothetical world from a complete 23-column `map_tokens` hypothesis.
It does not clone the real game, so hidden source-world state cannot enter the
rollout.

```python
observed = env.player_map_numpy()
completed = predicted_tokens.copy()

# Observed information is authoritative, not the prediction.
visible = observed[:, 0] == 1
completed[visible] = observed[visible]
completed[:, 0] = observed[:, 0]

rollout_env = env.make_belief_env(completed)
```

`perspective` is optional and defaults to the current player:

```python
rollout_env = env.make_belief_env(completed, perspective=player_id)
```

For many parallel searches over that hypothesis, pass the detached belief
environment to `MctsPool`, never the original hidden-state source:

```python
from PolyEnv import MctsPool

pool = MctsPool(rollout_env, num_trees=128)
```

See [MctsPool: Native Batched PUCT](mcts_pool.md) for the batched evaluator
loop.

For end-to-end batched self-play, use `SelfPlayPool` instead. Its
`submit_beliefs(state_ids, completed_map_tokens)` API performs the same
visible-row validation directly on a contiguous batch and creates detached
MCTS roots without returning a live `GameEnv` or a full map:

```python
request = self_play_pool.reset()
completed = external_belief_model(request)
self_play_pool.submit_beliefs(request["state_id"], completed)
```

`full_map_numpy()` is suitable for offline labels or tests only; it must not
be supplied as an input to fog-of-war self-play. See
[SelfPlayPool: Native Belief-MCTS Self-Play](self_play_pool.md).

## Validation and isolation

The input must have one 23-integer row per map tile. The engine rejects:

- a changed visibility mask;
- any difference on a currently revealed row (`revealed tiles differ: ...`);
- malformed token rows or invalid token values.

The returned environment is a new `GameSession`. It preserves only runtime
state that is legal for the selected player to know and builds hidden tiles,
cities, and units from `completed_map_tokens`. It shares no hidden units,
cities, caches, replay records, or visible-event journal with the source
environment.

`env.clone()` is for perfect-information/debug branches. It retains the real
hidden state and must not be used for fog-of-war MCTS.

For every token column and valid value, see
[Game Data And IDs](token_reference.md).
