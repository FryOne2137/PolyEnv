from __future__ import annotations

import numpy as np

from game_engine import GameEnv, tribes


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


def test_copy_alias_matches_clone_state() -> None:
    env = _env()

    assert env.copy().model_request()["map_tokens"] == env.clone().model_request()["map_tokens"]
