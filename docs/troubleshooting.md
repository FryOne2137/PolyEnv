# Troubleshooting

## `ModuleNotFoundError: No module named 'game_engine'`

The package is not installed in the Python environment you are using.

Use:

```bash
python -m pip install .
```

or from GitHub:

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
```

Check that `python` and `pip` refer to the same environment:

```bash
python -m pip --version
python -c "import sys; print(sys.executable)"
```

## `model_request_numpy()` Fails Because NumPy Is Missing

Install the latest package metadata. NumPy is declared as a runtime dependency:

```bash
python -m pip install --upgrade git+https://github.com/FryOne2137/PolyEnv.git
```

For local development:

```bash
python -m pip install -e .
```

## Read the Docs Cannot Find `docs/requirements.txt`

The file exists locally but was not pushed to GitHub.

```bash
git status --short
git add .readthedocs.yaml mkdocs.yml docs/
git commit -m "Add documentation"
git push origin main
```

Then rebuild the project in Read the Docs.

## Illegal Action Selected

Only choose actions from the current packet:

```python
packet = env.model_request_numpy()
legal_action_ids = packet["actions"]["action_id"]
action_id = int(legal_action_ids[0])
env.step_fast(action_id)
```

Do not use the row index as the action id:

```python
# Wrong if row is just the argmax row in the action list.
env.step_fast(row)
```

Instead:

```python
row = int(scores.argmax())
action_id = int(packet["actions"]["action_id"][row])
env.step_fast(action_id)
```

## `model_request_numpy()` Does Not Show Hidden Tiles

This is expected. Model request packets are player-view:

```python
packet = env.model_request_numpy()
visible = packet["map_tokens"]
```

Use the explicit full-map API only for ground-truth labels or debugging:

```python
target = env.full_map_numpy()
```

Do not use `full_map_numpy()` as normal policy input unless you intentionally want an omniscient/debug baseline.

## Torch GPU Transfer Is Slow

Batch before moving to GPU:

```python
map_tokens = torch.stack([
    torch.from_numpy(packet["map_tokens"])
    for packet in packets
])
map_tokens = map_tokens.to("cuda", non_blocking=True)
```

Avoid this pattern:

```python
for packet in packets:
    torch.from_numpy(packet["map_tokens"]).to("cuda")
```

Many small CPU-to-GPU transfers are slower than one larger batched transfer.
