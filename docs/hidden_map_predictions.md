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
