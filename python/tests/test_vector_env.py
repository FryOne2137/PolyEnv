from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import Bardur, GameEnv, Imperius, VectorGameEnv


_VISIBLE_EVENT_FEATURE_COLUMNS = (
    "round", "turn", "type_id", "flags", "source_index", "target_index",
    "damage", "hp_before", "hp_after", "actor_player", "actor_tribe",
    "tile_action_kind", "building_type", "spawn_type", "source_unit_type",
    "target_unit_type", "source_observed_unit_id", "target_observed_unit_id",
    "source_unit_hp_before", "source_unit_hp_after", "target_unit_hp_before",
    "target_unit_hp_after", "unit_upgrade_kind", "upgraded_unit_type",
    "unit_destroyed", "source_unit_destroyed",
)

_VISIBLE_EVENT_AFFECTED_COLUMNS = (
    "affected_observed_unit_id", "affected_tile_index", "affected_unit_type",
    "affected_damage", "affected_hp_before", "affected_hp_after",
    "affected_destroyed", "affected_splash",
)


def _single(seed: int) -> GameEnv:
    return GameEnv(seed=seed, map_size=11, players=(Bardur, Imperius))


def _allocate_batch_buffers(vector: VectorGameEnv) -> dict[str, np.ndarray]:
    """Allocate the exact external-buffer schema exposed by VectorGameEnv."""
    return {
        name: np.empty(tuple(field["shape"]), dtype=field["dtype"])
        for name, field in vector.batch_spec().items()
    }


def _assert_same_batch(expected: dict[str, np.ndarray], actual: dict[str, np.ndarray]) -> None:
    assert actual.keys() == expected.keys()
    for name, value in expected.items():
        np.testing.assert_array_equal(actual[name], value, err_msg=name)


def _batch_rows(batch: dict[str, np.ndarray], env_ids: np.ndarray) -> dict[str, np.ndarray]:
    """Return one dense batch row subset, preserving the slot row order."""
    return {name: value[env_ids] for name, value in batch.items()}


def test_vector_reset_matches_single_environments() -> None:
    vector = VectorGameEnv(
        num_envs=4,
        seed=1234,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
    )

    batch = vector.reset(seed=1234)

    assert batch["map_tokens"].shape == (4, 121, 23)
    assert batch["map_tokens"].dtype == np.int32
    assert batch["state"].shape == (4, 11)
    assert batch["action_id"].shape == (4, 128)
    assert batch["action_features"].shape == (4, 128, 17)
    assert batch["action_arg_mask"].shape == (4, 128, 12)
    assert batch["action_mask"].dtype == np.uint8
    assert batch["visible_event_features"].shape == (4, 0, 26)
    assert batch["visible_event_sequence"].dtype == np.uint64
    assert batch["visible_event_affected"].shape == (4, 0, 9, 8)
    assert batch["visible_event_mask"].dtype == np.uint8
    assert np.all(batch["action_valid"] == 0)
    assert np.array_equal(batch["env_id"], np.arange(4, dtype=np.int32))

    for index in range(4):
        env = _single(1234 + index)
        legal_ids = np.asarray(env.legal_action_ids_fast(), dtype=np.int32)
        count = int(batch["legal_action_count"][index])

        assert np.array_equal(batch["map_tokens"][index], env.player_map_numpy())
        assert count == len(legal_ids)
        assert np.array_equal(batch["action_id"][index, :count], legal_ids)
        assert np.all(batch["action_mask"][index, :count] == 1)
        assert np.all(batch["action_mask"][index, count:] == 0)
        assert np.all(batch["action_id"][index, count:] == -1)


def test_vector_first_reset_uses_constructor_seed_sequence() -> None:
    vector = VectorGameEnv(num_envs=2, seed=81, max_actions=128)
    batch = vector.reset()

    for index in range(2):
        assert np.array_equal(batch["map_tokens"][index], _single(81 + index).player_map_numpy())


def test_vector_step_matches_single_environment_trajectory() -> None:
    vector = VectorGameEnv(
        num_envs=3,
        seed=97,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
    )
    batch = vector.reset(seed=97)
    singles = [_single(97 + index) for index in range(3)]

    for _ in range(6):
        # Prefer the final legal row: on these seeded games it includes board
        # mutations, so the test covers more than repeated end-turn actions.
        rows = batch["legal_action_count"] - 1
        actions = batch["action_id"][np.arange(3), rows].copy()
        stepped = vector.step(actions)

        assert np.all(stepped["action_valid"] == 1)
        assert np.all(stepped["terminated"] == 0)
        assert np.all(stepped["reward"] == 0.0)
        for index, action_id in enumerate(actions):
            ok, done, reward, _, _ = singles[index].step_fast(int(action_id))
            assert ok is True
            assert done is False
            assert reward == 0.0
            assert np.array_equal(stepped["map_tokens"][index], singles[index].player_map_numpy())
        batch = stepped


def test_vector_rejects_wrong_action_batch_shape() -> None:
    vector = VectorGameEnv(num_envs=2, seed=5, max_actions=128)
    with pytest.raises(ValueError, match="num_envs"):
        vector.step(np.array([0], dtype=np.int32))


def test_vector_position_ids_track_state_and_episode_generations() -> None:
    vector = VectorGameEnv(
        num_envs=3,
        seed=47,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
    )
    initial = vector.reset(seed=47)

    assert initial["state_id"].dtype == np.uint64
    assert initial["episode_id"].dtype == np.uint64
    assert len(np.unique(initial["state_id"])) == vector.num_envs
    assert len(np.unique(initial["episode_id"])) == vector.num_envs
    assert set(("state_id", "episode_id")) <= set(vector.batch_spec())

    assert np.all(initial["legal_action_count"] > 0)
    actions = initial["action_id"][:, 0].copy()
    stepped = vector.step_checked(actions, initial["state_id"])

    assert np.all(stepped["action_valid"] == 1)
    assert np.all(stepped["terminated"] == 0)
    assert np.all(stepped["state_id"] != initial["state_id"])
    # A normal non-terminal transition changes the position, not the episode.
    assert np.array_equal(stepped["episode_id"], initial["episode_id"])

    reset = vector.reset(seed=47)
    assert np.all(reset["state_id"] != stepped["state_id"])
    assert np.all(reset["episode_id"] != stepped["episode_id"])


def test_vector_step_checked_rejects_mixed_stale_ids_atomically() -> None:
    vector = VectorGameEnv(
        num_envs=3,
        seed=53,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
    )
    initial = vector.reset(seed=53)
    first_actions = initial["action_id"][:, 0].copy()
    current = vector.step_checked(first_actions, initial["state_id"])

    # One old row is enough to reject the complete action vector.  Snapshot
    # through observe_slots(), whose output itself does not advance a state.
    env_ids = np.arange(vector.num_envs, dtype=np.int32)
    before_rejection = vector.observe_slots(env_ids)
    stale_ids = current["state_id"].copy()
    stale_ids[-1] = initial["state_id"][-1]
    actions = before_rejection["action_id"][:, 0].copy()

    with pytest.raises(ValueError, match="stale or mismatched"):
        vector.step_checked(actions, stale_ids)

    after_rejection = vector.observe_slots(env_ids)
    _assert_same_batch(before_rejection, after_rejection)

    # The still-current token is accepted, proving that the rejected call did
    # not partially consume actions from any environment.
    accepted = vector.step_checked(actions, before_rejection["state_id"])
    assert np.all(accepted["action_valid"] == 1)
    assert np.all(accepted["state_id"] != before_rejection["state_id"])


def test_vector_slots_match_full_batch_and_reject_duplicate_or_stale_rows() -> None:
    kwargs = dict(
        num_envs=5,
        seed=59,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
    )
    slotted = VectorGameEnv(**kwargs)
    reference = VectorGameEnv(**kwargs)
    expected_initial = reference.reset(seed=59)
    slotted.reset(seed=59)

    slots = [np.asarray(slot, dtype=np.int32) for slot in slotted.slot_partitions(2)]
    assert [slot.size for slot in slots] == [3, 2]
    assert all(slot.dtype == np.int32 for slot in slots)
    assert np.array_equal(np.sort(np.concatenate(slots)), np.arange(5, dtype=np.int32))

    full_actions = expected_initial["action_id"][:, 0].copy()
    expected_after_step = reference.step_checked(full_actions, expected_initial["state_id"])
    for env_ids in slots:
        observed = slotted.observe_slots(env_ids)
        _assert_same_batch(_batch_rows(expected_initial, env_ids), observed)
        slot_actions = observed["action_id"][:, 0].copy()
        stepped = slotted.step_slots_checked(env_ids, slot_actions, observed["state_id"])
        _assert_same_batch(_batch_rows(expected_after_step, env_ids), stepped)

    env_ids = slots[0]
    current = slotted.observe_slots(env_ids)
    duplicate_ids = np.array([env_ids[0], env_ids[0]], dtype=np.int32)
    duplicate_actions = np.array([current["action_id"][0, 0]] * 2, dtype=np.int32)
    duplicate_states = np.array([current["state_id"][0]] * 2, dtype=np.uint64)
    with pytest.raises(ValueError, match="duplicate"):
        slotted.observe_slots(duplicate_ids)
    with pytest.raises(ValueError, match="duplicate"):
        slotted.step_slots_checked(duplicate_ids, duplicate_actions, duplicate_states)
    _assert_same_batch(current, slotted.observe_slots(env_ids))

    stale_ids = current["state_id"].copy()
    stale_ids[-1] = expected_initial["state_id"][env_ids[-1]]
    with pytest.raises(ValueError, match="stale or mismatched"):
        slotted.step_slots_checked(
            env_ids, current["action_id"][:, 0].copy(), stale_ids
        )
    _assert_same_batch(current, slotted.observe_slots(env_ids))


def test_vector_marks_invalid_actions_without_mutating_state() -> None:
    vector = VectorGameEnv(num_envs=2, seed=6, max_actions=128)
    before = vector.reset(seed=6)
    after = vector.step(np.array([-1, -1], dtype=np.int32))

    assert np.all(after["action_valid"] == 0)
    assert np.all(after["reward"] == 0.0)
    assert np.all(after["terminated"] == 0)
    assert np.array_equal(after["map_tokens"], before["map_tokens"])


def test_vector_truncates_action_rows_without_failing_the_rollout() -> None:
    vector = VectorGameEnv(num_envs=1, seed=11, max_actions=1)
    batch = vector.reset(seed=11)

    assert np.array_equal(batch["legal_action_count"], np.array([1], dtype=np.int32))
    assert int(batch["total_legal_action_count"][0]) > 1
    assert np.array_equal(batch["action_truncated"], np.array([1], dtype=np.uint8))
    assert np.array_equal(batch["action_mask"], np.array([[1]], dtype=np.uint8))

    # EndTurn is retained when the canonical prefix overflows, so the reduced
    # action set always has a progress action rather than stalling an episode.
    single = _single(11)
    end_turn = next(
        action["action_id"]
        for action in single.legal_param_actions()
        if action["type_fullname"] == "end_turn"
    )
    assert int(batch["action_id"][0, 0]) == end_turn

    stepped = vector.step(batch["action_id"][:, 0])
    assert np.array_equal(stepped["action_valid"], np.array([1], dtype=np.uint8))


def test_vector_visible_event_history_matches_single_game_env() -> None:
    history = 3
    vector = VectorGameEnv(
        num_envs=2,
        seed=2137,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
        visible_event_history=history,
    )
    batch = vector.reset(seed=2137)
    singles = [_single(2137 + index) for index in range(2)]

    assert vector.visible_event_history == history
    assert tuple(vector.visible_event_feature_names) == _VISIBLE_EVENT_FEATURE_COLUMNS
    assert tuple(vector.visible_event_affected_feature_names) == (
        "observed_unit_id", "tile_index", "unit_type", "damage", "hp_before",
        "hp_after", "destroyed", "splash",
    )
    assert batch["visible_event_features"].shape == (2, history, 26)
    assert batch["visible_event_affected"].shape == (2, history, 9, 8)
    assert np.all(batch["visible_event_mask"] == 0)

    # Select a move whenever it exists. Once a unit has spent its move, end
    # the turn so it becomes available again. This produces more than K
    # visible records while exercising changing player perspectives.
    for _ in range(12):
        actions = []
        for single in singles:
            legal = single.legal_param_actions()
            selected = next((action for action in legal if action["type"] == "move"), None)
            if selected is None:
                selected = next(action for action in legal if action["type"] == "end_turn")
            actions.append(selected["action_id"])
        actions = np.asarray(actions, dtype=np.int32)
        batch = vector.step(actions)
        assert np.all(batch["action_valid"] == 1)
        for single, action_id in zip(singles, actions):
            ok, _, _, _, _ = single.step_fast(int(action_id))
            assert ok is True

    assert np.any(batch["visible_event_mask"] == 1)
    for index, single in enumerate(singles):
        packet = single.visible_events_numpy(0)
        event_count = len(packet["sequence"])
        kept = min(history, event_count)
        first_row = history - kept

        mask = batch["visible_event_mask"][index]
        assert np.all(mask[:first_row] == 0)
        assert np.all(mask[first_row:] == 1)
        assert np.array_equal(
            batch["visible_event_sequence"][index, first_row:], packet["sequence"][-kept:]
        )
        assert np.array_equal(
            batch["visible_event_action_sequence"][index, first_row:],
            packet["action_sequence"][-kept:],
        )

        expected_features = np.stack(
            [packet[column][-kept:] for column in _VISIBLE_EVENT_FEATURE_COLUMNS], axis=-1
        ).astype(np.int32, copy=False)
        assert np.array_equal(batch["visible_event_features"][index, first_row:], expected_features)

        for local_event, packet_event in enumerate(range(event_count - kept, event_count)):
            row = first_row + local_event
            affected_start = int(packet["affected_offsets"][packet_event])
            affected_end = int(packet["affected_offsets"][packet_event + 1])
            affected_count = affected_end - affected_start
            assert affected_count <= 9
            assert np.all(batch["visible_event_affected_mask"][index, row, :affected_count] == 1)
            assert np.all(batch["visible_event_affected_mask"][index, row, affected_count:] == 0)
            if affected_count:
                expected_affected = np.stack(
                    [packet[column][affected_start:affected_end] for column in _VISIBLE_EVENT_AFFECTED_COLUMNS],
                    axis=-1,
                ).astype(np.int32, copy=False)
                assert np.array_equal(
                    batch["visible_event_affected"][index, row, :affected_count], expected_affected
                )


def test_vector_rejects_negative_visible_event_history() -> None:
    with pytest.raises(ValueError, match="visible_event_history"):
        VectorGameEnv(num_envs=1, visible_event_history=-1)


@pytest.mark.parametrize("visible_event_history", [0, 2])
def test_vector_into_matches_allocating_api_and_reuses_storage(visible_event_history: int) -> None:
    kwargs = dict(
        num_envs=3,
        seed=77,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
        visible_event_history=visible_event_history,
    )
    into = VectorGameEnv(**kwargs)
    reference = VectorGameEnv(**kwargs)
    buffers = _allocate_batch_buffers(into)
    storage_addresses = {name: array.ctypes.data for name, array in buffers.items()}

    into.reset_into(buffers, seed=77)
    expected = reference.reset(seed=77)
    _assert_same_batch(expected, buffers)
    assert {name: array.ctypes.data for name, array in buffers.items()} == storage_addresses

    # This is intentionally a strided view into an output buffer. step_into()
    # copies the tiny action vector before it starts overwriting output storage.
    selected_actions = buffers["action_id"][:, 0]
    expected = reference.step(selected_actions.copy())
    into.step_into(selected_actions, buffers)
    _assert_same_batch(expected, buffers)
    assert {name: array.ctypes.data for name, array in buffers.items()} == storage_addresses

    # The checked pinned-buffer boundary copies both the strided action view
    # and the state-id vector before overwriting this same slot.
    selected_actions = buffers["action_id"][:, 0]
    expected = reference.step_checked(selected_actions.copy(), expected["state_id"])
    into.step_into_checked(selected_actions, buffers["state_id"], buffers)
    _assert_same_batch(expected, buffers)
    assert {name: array.ctypes.data for name, array in buffers.items()} == storage_addresses


def test_vector_batch_spec_describes_every_external_buffer() -> None:
    vector = VectorGameEnv(num_envs=2, seed=7, max_actions=32, visible_event_history=1)
    spec = vector.batch_spec()
    batch = vector.reset(seed=7)

    assert set(spec) == set(batch)
    for name, field in spec.items():
        assert np.dtype(field["dtype"]) == batch[name].dtype
        assert tuple(field["shape"]) == batch[name].shape


def test_vector_into_rejects_bad_output_buffers_before_advancing_state() -> None:
    kwargs = dict(num_envs=2, seed=99, max_actions=64, auto_reset=False)
    vector = VectorGameEnv(**kwargs)
    reference = VectorGameEnv(**kwargs)
    start = vector.reset(seed=99)
    reference.reset(seed=99)
    buffers = _allocate_batch_buffers(vector)
    actions = start["action_id"][:, 0]

    wrong_dtype = dict(buffers)
    wrong_dtype["state"] = np.empty((2, 11), dtype=np.int64)
    with pytest.raises(ValueError, match="incorrect dtype"):
        vector.step_into(actions, wrong_dtype)

    # The rejected call has not consumed actions or advanced either game.
    _assert_same_batch(reference.step(actions.copy()), vector.step(actions.copy()))


def test_vector_into_validates_buffer_contract_and_does_not_forcecast_actions() -> None:
    vector = VectorGameEnv(num_envs=2, seed=17, max_actions=64)
    buffers = _allocate_batch_buffers(vector)

    missing = dict(buffers)
    del missing["state"]
    with pytest.raises(ValueError, match="missing required"):
        vector.reset_into(missing)

    non_contiguous = dict(buffers)
    non_contiguous["state"] = np.empty((2, 22), dtype=np.int32)[:, ::2]
    with pytest.raises(ValueError, match="C-contiguous"):
        vector.reset_into(non_contiguous)

    read_only = dict(buffers)
    frozen = buffers["state"].copy()
    frozen.setflags(write=False)
    read_only["state"] = frozen
    with pytest.raises(ValueError, match="writable"):
        vector.reset_into(read_only)

    unaligned = dict(buffers)
    unaligned_storage = np.empty(2 * 11 * np.dtype(np.int32).itemsize + 1, dtype=np.uint8)
    unaligned["state"] = unaligned_storage[1:].view(np.int32).reshape(2, 11)
    with pytest.raises(ValueError, match="aligned"):
        vector.reset_into(unaligned)

    overlapping = dict(buffers)
    overlapping["total_legal_action_count"] = overlapping["legal_action_count"]
    with pytest.raises(ValueError, match="overlaps"):
        vector.reset_into(overlapping)

    with pytest.raises(ValueError, match="int32"):
        vector.step_into(np.zeros(2, dtype=np.int64), buffers)
