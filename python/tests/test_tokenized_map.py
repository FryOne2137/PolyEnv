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
    assert all(len(tile) == 23 for tile in tiles)


def test_visible_city_unit_exposes_its_origin_city_id() -> None:
    env = GameEnv(
        seed=123,
        map_size=11,
        players=(tribes.Bardur, tribes.Imperius),
    )

    # The player's starting unit is trained by their capital (city id 0).
    own_units = [tile for tile in env.player_map() if tile[3] == 0 and tile[4] != -1]
    assert own_units
    assert own_units[0][22] == 0


def test_visible_city_exposes_its_workshop_wall_and_park_state() -> None:
    env = GameEnv(
        seed=123,
        map_size=11,
        players=(tribes.Bardur, tribes.Imperius),
    )

    # A fresh capital has no Workshop, City Wall, or Park.
    own_cities = [tile for tile in env.player_map() if tile[14] == 2 and tile[16] == 0]
    assert own_cities
    assert own_cities[0][10:13] == [0, 0, 0]
