# Polytopia like game engine

Polytopia-like game engine with Python bindings.
This is unofficial implementation ready for ai training. This is part of my university bachelor degree (I will attach a paper)

## How to use
I will make a documentation

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
