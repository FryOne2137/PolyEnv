from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import Bardur, GameEnv, Imperius, SelfPlayPool


def _scalar(seed: int) -> GameEnv:
    return GameEnv(seed=seed, map_size=11, players=(Bardur, Imperius))


def _completed_beliefs(seed: int, request: dict[str, np.ndarray]) -> np.ndarray:
    """Test-only complete hypotheses built from matching scalar games.

    Production callers must produce these rows from their own belief model;
    this helper intentionally lives in the test so SelfPlayPool never exposes
    an authoritative full map.
    """
    completed: list[np.ndarray] = []
    for index in range(request["map_tokens"].shape[0]):
        observed = request["map_tokens"][index]
        hypothesis = _scalar(seed + index).full_map_numpy().astype(np.int32, copy=True)
        visible = observed[:, 0] == 1
        hypothesis[:, 0] = observed[:, 0]
        hypothesis[visible] = observed[visible]
        completed.append(hypothesis)
    return np.stack(completed)


def _pool(seed: int = 401, *, auto_reset: bool = False) -> SelfPlayPool:
    return SelfPlayPool(
        num_envs=2,
        seed=seed,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=auto_reset,
    )


def test_self_play_pool_returns_dense_public_belief_requests() -> None:
    pool = _pool()
    request = pool.reset(seed=401)

    assert pool.num_envs == 2
    assert pool.num_threads == 2
    assert pool.search_active is False
    assert request["map_tokens"].shape == (2, 121, 23)
    assert request["map_tokens"].dtype == np.int32
    assert request["state"].shape == (2, 11)
    assert request["action_id"].shape == (2, 128)
    assert request["action_features"].shape == (2, 128, 17)
    assert request["action_arg_mask"].shape == (2, 128, 12)
    assert request["action_mask"].dtype == np.uint8
    assert request["state_id"].dtype == np.uint64
    assert request["episode_id"].dtype == np.uint64
    assert len(np.unique(request["state_id"])) == 2
    assert np.array_equal(request["env_id"], np.array([0, 1], dtype=np.int32))
    assert np.array_equal(request["to_play"], np.array([0, 0], dtype=np.int32))
    assert "full_map" not in request

    for index in range(2):
        scalar = _scalar(401 + index)
        legal = np.asarray(scalar.legal_action_ids_fast(), dtype=np.int32)
        count = int(request["legal_action_count"][index])
        assert np.array_equal(request["map_tokens"][index], scalar.player_map_numpy())
        assert np.array_equal(request["action_id"][index, :count], legal)
        assert np.all(request["action_mask"][index, :count] == 1)
        assert np.all(request["action_mask"][index, count:] == 0)


def test_self_play_pool_runs_detached_belief_mcts_and_steps_live_games() -> None:
    seed = 511
    pool = _pool(seed)
    request = pool.reset(seed=seed)
    completed = _completed_beliefs(seed, request)

    pool.submit_beliefs(request["state_id"], completed)
    assert pool.search_active is True

    leaves = pool.select_leaves()
    assert pool.pending_count == 2
    assert leaves["map_tokens"].shape == (2, 121, 23)
    assert np.array_equal(leaves["env_id"], np.array([0, 1], dtype=np.int32))
    assert np.array_equal(leaves["state_id"], request["state_id"])
    # The belief world's hidden completion is available to the rollout only;
    # the neural evaluator still receives precisely the live player's view.
    assert np.array_equal(leaves["map_tokens"], request["map_tokens"])

    waiting = pool.select_leaves()
    assert waiting["leaf_id"].shape == (0,)

    pool.expand_and_backup(
        leaves["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        np.array([0.25, -0.5], dtype=np.float32),
    )
    assert pool.pending_count == 0

    root = pool.root_policy(temperature=0.0)
    assert np.array_equal(root["state_id"], request["state_id"])
    assert np.array_equal(root["root_visit_count"], np.array([1, 1], dtype=np.int32))
    assert np.all(root["policy"].sum(axis=1) == 1.0)
    action_ids = root["action_id"][:, 0].copy()
    assert np.all(action_ids >= 0)

    expected = [_scalar(seed + index) for index in range(2)]
    for env, action_id in zip(expected, action_ids):
        ok, done, reward, _, _ = env.step_fast(int(action_id))
        assert ok is True
        assert done is False
        assert reward == 0.0

    next_request = pool.step(action_ids)
    assert pool.search_active is False
    assert np.all(next_request["action_valid"] == 1)
    assert np.all(next_request["terminated"] == 0)
    assert np.all(next_request["state_id"] != request["state_id"])
    for index, env in enumerate(expected):
        assert np.array_equal(next_request["map_tokens"][index], env.player_map_numpy())


def test_self_play_pool_rejects_stale_or_invalid_beliefs_atomically() -> None:
    seed = 613
    pool = _pool(seed)
    request = pool.reset(seed=seed)
    completed = _completed_beliefs(seed, request)

    changed = completed.copy()
    visible_index = int(np.flatnonzero(request["map_tokens"][0, :, 0] == 1)[0])
    changed[0, visible_index, 19] = (int(changed[0, visible_index, 19]) + 1) % 5
    with pytest.raises(ValueError, match="revealed tiles differ"):
        pool.submit_beliefs(request["state_id"], changed)
    assert pool.search_active is False

    with pytest.raises(ValueError, match="contiguous int32"):
        pool.submit_beliefs(request["state_id"], completed.astype(np.int64))
    assert pool.search_active is False

    pool.submit_beliefs(request["state_id"], completed)
    leaves = pool.select_leaves()
    with pytest.raises(RuntimeError, match="pending"):
        pool.step(np.array([0, 0], dtype=np.int32))

    # Reset clears outstanding leaves. A late model answer cannot be routed to
    # the new position, and its old position ids cannot submit new beliefs.
    new_request = pool.reset(seed=seed)
    assert pool.pending_count == 0
    with pytest.raises(RuntimeError, match="submit_beliefs"):
        pool.expand_and_backup(
            leaves["leaf_id"],
            np.zeros((2, 128), dtype=np.float32),
            np.zeros(2, dtype=np.float32),
        )
    with pytest.raises(ValueError, match="stale"):
        pool.submit_beliefs(request["state_id"], _completed_beliefs(seed, new_request))


def test_self_play_pool_keeps_hidden_belief_tokens_out_of_model_packets() -> None:
    seed = 719
    pool = _pool(seed)
    request = pool.reset(seed=seed)
    completed = _completed_beliefs(seed, request)

    hidden = int(np.flatnonzero(request["map_tokens"][0, :, 0] == 0)[0])
    # Alter a legal hidden terrain enum in the model's hypothesis. It must not
    # appear in the model packet because the packet remains player-visible.
    completed[0, hidden, 19] = (int(completed[0, hidden, 19]) + 1) % 5
    pool.submit_beliefs(request["state_id"], completed)
    leaves = pool.select_leaves()

    assert np.array_equal(leaves["map_tokens"], request["map_tokens"])
    assert np.all(leaves["map_tokens"][0, hidden] == request["map_tokens"][0, hidden])
