# Troubleshooting

## `No module named 'PolyEnv'`

Install PolyEnv with the same interpreter that runs the program:

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
python -c "import PolyEnv; print(PolyEnv.__file__)"
```

## A Replay Does Not Load

Replay files require a compatible ruleset. Current `v2` files identify that
ruleset. A legacy `v1` replay saved before a map generator or action-space
change must be opened with the engine version that created it.

## An Action Is Rejected

Choose the action id from the current packet, not the index of a row:

```python
packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])
env.step_fast(action_id)
```

## The Map Shows Hidden Information

Use `model_request_numpy()`, `observation()`, or `player_map_numpy()` for a
player view. `full_map_numpy()` intentionally exposes ground truth and should
be used only for labels or debugging.

## Local Documentation

```bash
python -m pip install -r docs/requirements.txt
mkdocs serve
```

For Read the Docs setup, see [Read the Docs](readthedocs.md).
