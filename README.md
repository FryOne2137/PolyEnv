# Polytopia like game engine

Polytopia-like game engine with Python bindings.
This is unofficial implementation ready for ai training. This is part of my university bachelor degree (I will attach a paper)



## How to use

Documentation lives in `docs/` and can be published with Read the Docs.

Read the Docs configuration is already included in `.readthedocs.yaml`. Import this GitHub repository at [readthedocs.org](https://readthedocs.org/) and it will build the MkDocs site automatically.

```bash
pip install git+https://github.com/FryOne2137/PolyEnv.git
```

Local documentation preview:

```bash
pip install -r docs/requirements.txt
mkdocs serve
```

 

## Python quick start

```python
from game_engine import GameEnv, Bardur, Kickoo

env = GameEnv(seed=2137, map_size=16, tribes=[Bardur, Kickoo])
```

You can also pick tribes by name:

```python
from game_engine import get_tribe

bardur = get_tribe("Bardur")
```
