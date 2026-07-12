from __future__ import annotations

from PolyEnv import GameEnv, tribes


def test_tokenized_map_has_expected_shape() -> None:
    map_size = 11
    env = GameEnv(
        seed=123,
        map_size=map_size,
        players=(tribes.Bardur, tribes.Imperius),
    )

    tiles = env.tokenized_map()

    assert len(tiles) == map_size * map_size
    assert all(isinstance(tile, list) for tile in tiles)
    assert all(len(tile) == 19 for tile in tiles)
