from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import Bardur, GameEnv, Imperius, VectorGameEnv


def _single(seed: int) -> GameEnv:
    return GameEnv(seed=seed, map_size=11, players=(Bardur, Imperius))


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


def test_vector_marks_invalid_actions_without_mutating_state() -> None:
    vector = VectorGameEnv(num_envs=2, seed=6, max_actions=128)
    before = vector.reset(seed=6)
    after = vector.step(np.array([-1, -1], dtype=np.int32))

    assert np.all(after["action_valid"] == 0)
    assert np.all(after["reward"] == 0.0)
    assert np.all(after["terminated"] == 0)
    assert np.array_equal(after["map_tokens"], before["map_tokens"])


def test_vector_reports_action_capacity_overflow() -> None:
    vector = VectorGameEnv(num_envs=1, seed=11, max_actions=1)
    with pytest.raises(RuntimeError, match="max_actions=1"):
        vector.reset(seed=11)
