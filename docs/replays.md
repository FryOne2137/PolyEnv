# Replays

A `.polygame` file stores a game setup and the accepted action ids. It is an
action replay, not a full state snapshot.

```python
env.save("match.polygame")

replayed = GameEnv()
final_observation = replayed.load("match.polygame")
```

The GUI uses the same format. Loading a replay in the GUI opens a read-only
viewer with a timeline.

## Compatibility

Current replay files use format `v2` and include a ruleset identifier. A replay
can be loaded only by a compatible engine ruleset. This is necessary because
action ids depend on the map generator and action space.

Legacy `v1` files cannot be replayed after incompatible engine changes. Keep a
replay together with the PolyEnv version that created it.

## Replays And MCTS

Use `clone()` or `save_state()` / `load_state()` for in-memory branches. They
are faster and intended for MCTS. Use `save()` only when a game needs to be
stored or shared as a file.
