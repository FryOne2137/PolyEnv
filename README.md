# PolyEnv

PolyEnv is an unofficial Polytopia-like game engine with a C++ core and Python bindings.

It is built as a game environment for AI experiments, external bots, MCTS rollouts, and training pipelines.

## What PolyEnv Is

- A Python-installable game engine package.
- A CPU game simulator with legal actions and fast stepping.
- A source of player-view observations for models.
- A source of explicit full-map ground truth for debugging or supervised hidden-map prediction.
- A rules engine scoped to the 12 regular Polytopia tribes.
- A project created as part of university bachelor degree work.

## What PolyEnv Is Not

- It is not affiliated with or endorsed by The Battle of Polytopia or its creators.
- It is not a full clone of the official game.
- It does not support special tribe mechanics such as Aquarion, Elyrion, Polaris, or Cymanti.
- It is not a trained bot or model.
- It does not implement reward shaping; that belongs in the training/model repository.
- It does not expose hidden map information through the normal model API.

## Installation

Install from GitHub:

```bash
python -m pip install git+https://github.com/FryOne2137/PolyEnv.git
```

Install from a local checkout:

```bash
git clone https://github.com/FryOne2137/PolyEnv.git
cd PolyEnv
python -m pip install .
```

For development:

```bash
python -m pip install -e .
```

## Minimal Example

```python
from PolyEnv import GameEnv, Bardur, Imperius

env = GameEnv(
    seed=1234,
    map_size=11,
    players=(Bardur, Imperius),
)

packet = env.model_request_numpy()
action_id = int(packet["actions"]["action_id"][0])

ok, done, reward, winner, current_player = env.step_fast(action_id)
```

`model_request_numpy()` returns the current player's visible map, player/game metadata, legal actions, and format metadata.

Use `env.full_map_numpy()` only when you explicitly need full ground truth for debugging or training labels.

## Documentation

Documentation lives in `docs/` and can be served locally:

```bash
python -m pip install -r docs/requirements.txt
mkdocs serve
```

Read the Docs configuration is included in `.readthedocs.yaml`.

## Attribution

The map generation code is based on [QuasiStellar/Polytopia-Map-Generator](https://github.com/QuasiStellar/Polytopia-Map-Generator) and has been modified for this engine.
