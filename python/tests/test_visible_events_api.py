from __future__ import annotations

import numpy as np

from PolyEnv import GameEnv, tribes


def _env() -> GameEnv:
    return GameEnv(seed=2137, map_size=11, players=(tribes.Bardur, tribes.Imperius))


def test_visible_events_numpy_has_compact_event_layout() -> None:
    env = _env()
    action_id = env.legal_action_ids_fast()[0]
    env.step_fast(action_id)

    packet = env.visible_events_numpy(0)
    event_count = len(packet["sequence"])

    for key in [
        "sequence", "action_sequence", "round", "turn", "type_id", "flags",
        "source_index", "target_index", "damage", "hp_before", "hp_after",
        "actor_player", "actor_tribe", "tile_action_kind", "building_type",
        "spawn_type", "source_unit_type", "target_unit_type",
        "source_observed_unit_id", "target_observed_unit_id",
        "source_unit_hp_before", "source_unit_hp_after",
        "target_unit_hp_before", "target_unit_hp_after",
        "unit_upgrade_kind", "upgraded_unit_type", "unit_destroyed",
        "source_unit_destroyed",
    ]:
        assert isinstance(packet[key], np.ndarray)
        assert packet[key].shape == (event_count,)

    affected_count = len(packet["affected_observed_unit_id"])
    for key in [
        "affected_observed_unit_id", "affected_tile_index", "affected_unit_type",
        "affected_damage", "affected_hp_before", "affected_hp_after",
        "affected_destroyed", "affected_splash",
    ]:
        assert isinstance(packet[key], np.ndarray)
        assert packet[key].shape == (affected_count,)
    assert packet["affected_offsets"].shape == (event_count + 1,)
    assert int(packet["affected_offsets"][0]) == 0
    assert int(packet["affected_offsets"][-1]) == affected_count



def test_visible_events_cursor_returns_only_new_events() -> None:
    env = _env()
    env.step_fast(env.legal_action_ids_fast()[0])

    first = env.visible_events_numpy(0)
    cursor = int(first["next_cursor"])
    second = env.visible_events_numpy(cursor)

    assert len(second["sequence"]) == 0
    assert int(second["next_cursor"]) == cursor


def test_end_turn_is_not_a_visible_world_event() -> None:
    env = _env()
    end_turn = next(action["action_id"] for action in env.legal_param_actions()
                    if action["type"] == "end_turn")
    env.step_fast(end_turn)

    # The current player changed, but no map/unit action took place.
    packet = env.visible_events_numpy(0)
    assert len(packet["sequence"]) == 0


def test_affected_units_use_numpy_csr_not_python_objects() -> None:
    env = _env()
    env.step_fast(env.legal_action_ids_fast()[0])
    packet = env.visible_events_numpy(0)

    assert "affected_units" not in packet
    assert isinstance(packet["affected_offsets"], np.ndarray)
    assert packet["affected_offsets"].dtype == np.int64
