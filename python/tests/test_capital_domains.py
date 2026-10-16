from __future__ import annotations

import math

import pytest

from PolyEnv import Drylands, GameEnv, Lakes, WaterWorld

_FEAT_CAPITAL_LAYER = 9
_FEAT_ROAD_BRIDGE = 7
_FEAT_SETTLEMENT_TYPE = 14
_FEAT_RESOURCE = 18
_FEAT_BASE_TERRAIN = 19
_FEAT_TRIBE = 20

_RESOURCE_FISH = 1
_RESOURCE_FRUIT = 3
_RESOURCE_CROPS = 4
_RESOURCE_ANIMAL = 5

_TERRAIN_WATER = 1
_TERRAIN_LAND = 2
_TERRAIN_FOREST = 4

_ROAD = 1
_SETTLEMENT_VILLAGE = 1
_SETTLEMENT_CITY = 2


def _capital_positions(player_count: int, seed: int) -> list[tuple[int, int]]:
    map_size = 11
    players = tuple((index % 12) + 1 for index in range(player_count))
    env = GameEnv(seed=seed, map_size=map_size, players=players)
    return [
        (index % map_size, index // map_size)
        for index, tile in enumerate(env.full_map())
        if tile[_FEAT_CAPITAL_LAYER] == 1
    ]


def _domain_index(coordinate: int, map_size: int, domains_per_axis: int) -> int:
    for domain in range(domains_per_axis):
        start = domain * map_size // domains_per_axis
        end = (domain + 1) * map_size // domains_per_axis
        if start <= coordinate < end:
            return domain
    raise AssertionError(f"Coordinate outside map domains: {coordinate}")


def _domain_center(domain: int, map_size: int, domains_per_axis: int) -> int:
    start = domain * map_size // domains_per_axis
    end = (domain + 1) * map_size // domains_per_axis
    return min(end - 1, (start + end) // 2)


@pytest.mark.parametrize(
    ("player_count", "domains_per_axis"),
    [(2, 2), (5, 3), (10, 4)],
)
def test_capitals_use_distinct_quadrant_domains(
    player_count: int, domains_per_axis: int
) -> None:
    map_size = 11
    capitals = _capital_positions(player_count, seed=1234)

    assert len(capitals) == player_count
    domains = {
        (
            _domain_index(x, map_size, domains_per_axis),
            _domain_index(y, map_size, domains_per_axis),
        )
        for x, y in capitals
    }
    assert len(domains) == player_count

    for x, y in capitals:
        domain_x = _domain_index(x, map_size, domains_per_axis)
        domain_y = _domain_index(y, map_size, domains_per_axis)
        assert abs(x - _domain_center(domain_x, map_size, domains_per_axis)) <= 2
        assert abs(y - _domain_center(domain_y, map_size, domains_per_axis)) <= 2


def test_capital_domain_assignment_is_seed_deterministic() -> None:
    assert _capital_positions(10, seed=9876) == _capital_positions(10, seed=9876)


def _reference_sector_candidates(map_size: int, player_count: int) -> list[set[tuple[int, int]]]:
    """Candidate cells used by the reference generator's supported grid sizes."""
    sectors_per_axis = math.ceil(math.sqrt(player_count))
    base_span = map_size // sectors_per_axis
    assert base_span >= 3
    remainder = map_size - base_span * sectors_per_axis

    def sector_nudge(coordinate: int) -> int:
        return min(remainder, max(1, min(remainder, coordinate) - 1))

    result: list[set[tuple[int, int]]] = []
    padding = 2 if base_span > 4 else 1
    for sy in range(sectors_per_axis):
        for sx in range(sectors_per_axis):
            start_x = sx * base_span + sector_nudge(sx)
            start_y = sy * base_span + sector_nudge(sy)
            cells = {
                (x, y)
                for y in range(max(2, start_y + padding), min(map_size - 2, start_y + base_span - padding))
                for x in range(max(2, start_x + padding), min(map_size - 2, start_x + base_span - padding))
            }
            result.append(cells)
    return result


@pytest.mark.parametrize(("map_size", "player_count"), [(11, 2), (11, 5), (16, 2), (16, 5), (16, 10)])
@pytest.mark.parametrize("seed", [1, 1234, 9876])
def test_capitals_use_reference_sector_candidates(map_size: int, player_count: int, seed: int) -> None:
    players = tuple((index % 12) + 1 for index in range(player_count))
    env = GameEnv(seed=seed, map_size=map_size, players=players)
    capitals = [
        (index % map_size, index // map_size)
        for index, tile in enumerate(env.full_map())
        if tile[_FEAT_CAPITAL_LAYER] == 1
    ]

    reference_sectors = _reference_sector_candidates(map_size, player_count)
    assigned_sectors = [
        sector_index
        for capital in capitals
        for sector_index, candidate_cells in enumerate(reference_sectors)
        if capital in candidate_cells
    ]
    assert len(capitals) == player_count
    assert len(assigned_sectors) == player_count
    assert len(set(assigned_sectors)) == player_count


@pytest.mark.parametrize("map_type", [Lakes, Drylands, WaterWorld])
@pytest.mark.parametrize("seed", [1, 1234, 9876])
def test_every_village_and_city_has_a_road(map_type: int, seed: int) -> None:
    env = GameEnv(
        seed=seed,
        map_size=16,
        players=(1, 2, 3, 4),
        map_type=map_type,
    )
    settlements = [
        tile
        for tile in env.full_map()
        if tile[_FEAT_SETTLEMENT_TYPE] in (_SETTLEMENT_VILLAGE, _SETTLEMENT_CITY)
    ]

    assert settlements
    assert all(tile[_FEAT_ROAD_BRIDGE] == _ROAD for tile in settlements)


def test_water_world_has_deterministic_high_water_coverage() -> None:
    first = GameEnv(seed=1234, map_size=16, players=(1, 2, 3, 4), map_type=WaterWorld)
    second = GameEnv(seed=1234, map_size=16, players=(1, 2, 3, 4), map_type="water_world")

    assert first.observation()["map_type"] == "water_world"
    assert first.full_map() == second.full_map()

    water_tiles = sum(
        tile[_FEAT_BASE_TERRAIN] in (0, _TERRAIN_WATER)
        for tile in first.full_map()
    )
    water_fraction = water_tiles / (16 * 16)
    assert 0.82 <= water_fraction <= 0.88


@pytest.mark.parametrize(
    ("tribe", "resource", "terrain", "required_count"),
    [
        (2, _RESOURCE_FRUIT, _TERRAIN_LAND, 2),  # Imperius
        (3, _RESOURCE_ANIMAL, _TERRAIN_FOREST, 2),  # Bardur
        (5, _RESOURCE_FISH, _TERRAIN_WATER, 2),  # Kickoo
        (9, _RESOURCE_CROPS, _TERRAIN_LAND, 1),  # Zebasi
    ],
)
@pytest.mark.parametrize("seed", [1, 1234, 9876, 987654])
def test_turn_zero_tribes_have_required_capital_resources(
    tribe: int, resource: int, terrain: int, required_count: int, seed: int
) -> None:
    map_size = 11
    env = GameEnv(seed=seed, map_size=map_size, players=(tribe, 1))
    full_map = env.full_map()

    capital_index = next(
        index
        for index, tile in enumerate(full_map)
        if tile[_FEAT_CAPITAL_LAYER] == 1 and tile[_FEAT_TRIBE] == tribe
    )
    capital_x = capital_index % map_size
    capital_y = capital_index // map_size

    matching = 0
    for y in range(max(0, capital_y - 1), min(map_size, capital_y + 2)):
        for x in range(max(0, capital_x - 1), min(map_size, capital_x + 2)):
            if x == capital_x and y == capital_y:
                continue
            tile = full_map[y * map_size + x]
            matching += (
                tile[_FEAT_RESOURCE] == resource
                and tile[_FEAT_BASE_TERRAIN] == terrain
            )

    assert matching >= required_count
