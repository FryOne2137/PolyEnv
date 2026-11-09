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
    assert all(len(tile) == 24 for tile in tiles)


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


def test_city_population_is_visible_only_for_its_owner() -> None:
    env = GameEnv(
        seed=123,
        map_size=11,
        players=(tribes.Bardur, tribes.Imperius),
    )

    player_map = env.tokenized_map(player_id=0, visible_only=False)
    full_map = env.full_map()
    own_city_indices = [
        index for index, tile in enumerate(player_map)
        if tile[14] == 2 and tile[16] == 0
    ]
    foreign_city_indices = [
        index for index, tile in enumerate(player_map)
        if tile[14] == 2 and tile[16] == 1
    ]

    assert own_city_indices
    assert foreign_city_indices
    for index in own_city_indices:
        assert player_map[index][23] == full_map[index][23]
    for index in foreign_city_indices:
        assert player_map[index][23] == -1
