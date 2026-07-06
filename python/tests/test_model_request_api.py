from __future__ import annotations

import numpy as np

from game_engine import GameEnv, tribes

_FEAT_SETTLEMENT_TYPE = 11
_FEAT_BASE_TERRAIN = 16
_FEAT_TERRITORY_CITY_ID = 6
_NO_CITY = -1
_SETTLEMENT_CITY = 2
_BASE_TERRAIN_MOUNTAIN = 3
_BASE_TERRAIN_FOREST = 4
_TECH_COUNT = 25
_TILE_ACTION_COUNT = 14
_UNIT_UPGRADE_COUNT = 4
_BUILDING_COUNT = 16


def _env() -> GameEnv:
    return GameEnv(
        seed=1234,
        map_size=11,
        players=(tribes.Bardur, tribes.Imperius),
    )


def test_model_request_contains_canonical_packet_fields() -> None:
    env = _env()

    packet = env.model_request()

    assert set(packet.keys()) == {"map_tokens", "obs", "actions", "spec"}
    assert len(packet["map_tokens"]) == 11 * 11
    assert all(len(tile) == 18 for tile in packet["map_tokens"])
    assert len(packet["actions"]) == len(env.legal_param_actions())
    assert packet["spec"]["map_width"] == 11
    assert packet["spec"]["map_height"] == 11
    assert packet["obs"]["visible_only"] is True
    assert packet["map_tokens"] == env.player_map()
    assert packet["map_tokens"] == env.tokenized_map()
    assert packet["map_tokens"] != env.full_map()


def test_request_packet_alias_matches_model_request_shape() -> None:
    env = _env()

    packet = env.request_packet()

    assert set(packet.keys()) == {"map_tokens", "obs", "actions", "spec"}
    assert len(packet["actions"]) == len(env.model_request()["actions"])


def test_model_request_numpy_returns_arrays_for_hot_path_fields() -> None:
    env = _env()

    packet = env.model_request_numpy()
    actions = packet["actions"]

    assert isinstance(packet["map_tokens"], np.ndarray)
    assert packet["map_tokens"].shape == (11 * 11, 18)
    assert packet["map_tokens"].dtype == np.int32
    assert packet["obs"]["visible_only"] is True
    assert np.array_equal(packet["map_tokens"], env.player_map_numpy())
    assert not np.array_equal(packet["map_tokens"], env.full_map_numpy())

    action_count = len(env.legal_param_actions())
    for key in [
        "action_id",
        "type_id",
        "source_index",
        "target_index",
        "tech",
        "unit_id",
        "cost_stars",
        "damage_dealt",
        "damage_received",
    ]:
        assert isinstance(actions[key], np.ndarray)
        assert actions[key].shape == (action_count,)
        assert actions[key].dtype == np.int64

    assert isinstance(actions["arg_mask"], np.ndarray)
    assert actions["arg_mask"].shape == (action_count, 12)
    assert actions["arg_mask"].dtype == np.uint8
    assert len(actions["type"]) == action_count
    assert len(actions["type_fullname"]) == action_count


def test_request_packet_numpy_alias_matches_model_request_numpy_shape() -> None:
    env = _env()

    packet = env.request_packet_numpy()

    assert packet["map_tokens"].shape == env.model_request_numpy()["map_tokens"].shape


def test_found_city_actions_are_unit_actions_when_available() -> None:
    env = _env()

    for action in env.legal_param_actions():
        if action["type_fullname"] != "tile_action:found_city":
            continue

        assert action["unit_id"] >= 0
        assert action["arg_mask"][8] == 1


def test_build_actions_never_target_city_tiles() -> None:
    env = _env()
    full_map = env.full_map()

    for action in env.legal_param_actions():
        if action["type"] != "build":
            continue

        assert full_map[action["target_index"]][_FEAT_SETTLEMENT_TYPE] != _SETTLEMENT_CITY


def test_build_action_mask_is_zero_on_city_tiles() -> None:
    env = _env()
    spec = env.action_param_spec()
    width = spec["map_width"]
    cell_count = width * spec["map_height"]
    off_move = 1 + _TECH_COUNT
    off_attack = off_move + cell_count * cell_count
    off_heal = off_attack + cell_count * cell_count
    off_unit_upgrade = off_heal + cell_count
    off_tile_action = off_unit_upgrade + _UNIT_UPGRADE_COUNT * cell_count
    off_build = off_tile_action + _TILE_ACTION_COUNT * cell_count

    mask = env.legal_action_mask()
    city_indices = [
        idx
        for idx, tile in enumerate(env.full_map())
        if tile[_FEAT_SETTLEMENT_TYPE] == _SETTLEMENT_CITY
    ]
    assert city_indices

    for city_idx in city_indices:
        for building_idx in range(_BUILDING_COUNT):
            assert mask[off_build + building_idx * cell_count + city_idx] == 0


def test_roads_can_target_forest_tiles() -> None:
    env = GameEnv(
        seed=1,
        map_size=11,
        players=(tribes.Yadakk, tribes.Imperius),
    )
    full_map = env.full_map()

    forest_roads = [
        action
        for action in env.legal_param_actions()
        if action["type_fullname"] == "tile_action:build_road"
        and full_map[action["target_index"]][_FEAT_BASE_TERRAIN] == _BASE_TERRAIN_FOREST
    ]

    assert forest_roads


def test_bridges_can_target_neutral_tiles() -> None:
    env = GameEnv(
        seed=8,
        map_size=11,
        players=(tribes.Yadakk, tribes.Imperius),
    )
    full_map = env.full_map()

    neutral_bridges = [
        action
        for action in env.legal_param_actions()
        if action["type_fullname"] == "tile_action:build_bridge"
        and full_map[action["target_index"]][_FEAT_TERRITORY_CITY_ID] == _NO_CITY
    ]

    assert neutral_bridges


def test_bridges_can_connect_mountain_tiles() -> None:
    env = GameEnv(
        seed=63,
        map_size=11,
        players=(tribes.Yadakk, tribes.Imperius),
    )
    full_map = env.full_map()
    width = 11

    mountain_bridges = []
    for action in env.legal_param_actions():
        if action["type_fullname"] != "tile_action:build_bridge":
            continue

        idx = action["target_index"]
        x = idx % width
        y = idx // width
        vertical = (
            0 < y < width - 1
            and full_map[idx - width][_FEAT_BASE_TERRAIN] == _BASE_TERRAIN_MOUNTAIN
            and full_map[idx + width][_FEAT_BASE_TERRAIN] == _BASE_TERRAIN_MOUNTAIN
        )
        horizontal = (
            0 < x < width - 1
            and full_map[idx - 1][_FEAT_BASE_TERRAIN] == _BASE_TERRAIN_MOUNTAIN
            and full_map[idx + 1][_FEAT_BASE_TERRAIN] == _BASE_TERRAIN_MOUNTAIN
        )
        if vertical or horizontal:
            mountain_bridges.append(action)

    assert mountain_bridges


def test_copy_alias_matches_clone_state() -> None:
    env = _env()

    assert env.copy().model_request()["map_tokens"] == env.clone().model_request()["map_tokens"]
