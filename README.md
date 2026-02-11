# game-engine

Polytopia-like game engine with Python bindings.

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
