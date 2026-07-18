from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import Bardur, GameEnv, Imperius, Oumaji, SelfPlayPool


def _scalar(
    seed: int,
    players: tuple = (Bardur, Imperius),
) -> GameEnv:
    return GameEnv(seed=seed, map_size=11, players=players)


def _completed_beliefs(
    seed: int,
    request: dict[str, np.ndarray],
    players: tuple = (Bardur, Imperius),
) -> np.ndarray:
    """Test-only complete hypotheses built from matching scalar games.

    Production callers must produce these rows from their own belief model;
    this helper intentionally lives in the test so SelfPlayPool never exposes
    an authoritative full map.
    """
    completed: list[np.ndarray] = []
    for index in range(request["map_tokens"].shape[0]):
        observed = request["map_tokens"][index]
        hypothesis = _scalar(seed + index, players).full_map_numpy().astype(np.int32, copy=True)
        visible = observed[:, 0] == 1
        hypothesis[:, 0] = observed[:, 0]
        hypothesis[visible] = observed[visible]
        completed.append(hypothesis)
    return np.stack(completed)


def _pool(
    seed: int = 401,
    *,
    auto_reset: bool = False,
    players: tuple = (Bardur, Imperius),
    max_actions: int = 128,
) -> SelfPlayPool:
    return SelfPlayPool(
        num_envs=2,
        seed=seed,
        map_size=11,
        players=players,
        num_threads=2,
        max_actions=max_actions,
        auto_reset=auto_reset,
    )


def _allocate_belief_buffers(pool: SelfPlayPool) -> dict[str, np.ndarray]:
    return {
        name: np.empty(tuple(field["shape"]), dtype=field["dtype"])
        for name, field in pool.belief_batch_spec().items()
    }


def _allocate_buffers(spec: dict[str, dict[str, object]]) -> dict[str, np.ndarray]:
    return {
        name: np.empty(tuple(field["shape"]), dtype=field["dtype"])
        for name, field in spec.items()
    }


def _assert_same_packet(expected: dict[str, np.ndarray], actual: dict[str, np.ndarray]) -> None:
    assert actual.keys() == expected.keys()
    for name, value in expected.items():
        np.testing.assert_array_equal(actual[name], value, err_msg=name)


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
    assert request["winner"].dtype == np.int32
    assert request["terminal_values"].shape == (2, 2)
    assert np.all(request["winner"] == -1)
    assert np.all(request["terminal_values"] == 0.0)
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


def test_self_play_pool_runs_three_player_vector_value_mcts() -> None:
    seed = 823
    players = (Bardur, Imperius, Oumaji)
    pool = _pool(seed, players=players)
    request = pool.reset(seed=seed)
    completed = _completed_beliefs(seed, request, players)

    assert pool.player_count == 3
    assert pool.num_players == 3
    assert request["terminal_values"].shape == (2, 3)
    pool.submit_beliefs(request["state_id"], completed)
    leaves = pool.select_leaves()
    assert np.array_equal(leaves["player_count"], np.array([3, 3], dtype=np.int32))

    values = np.array(
        [[0.35, -0.10, -0.25], [-0.15, 0.55, -0.40]], dtype=np.float32
    )
    pool.expand_and_backup(
        leaves["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        values,
    )
    root = pool.root_policy()
    assert np.array_equal(root["player_count"], np.array([3, 3], dtype=np.int32))
    assert np.allclose(root["root_value"], values[:, 0])


def test_self_play_pool_truncates_action_rows_without_rejecting_a_search() -> None:
    seed = 907
    pool = _pool(seed, max_actions=1)
    request = pool.reset(seed=seed)
    assert np.all(request["legal_action_count"] == 1)
    assert np.all(request["total_legal_action_count"] > 1)
    assert np.all(request["action_truncated"] == 1)

    pool.submit_beliefs(request["state_id"], _completed_beliefs(seed, request))
    leaves = pool.select_leaves()
    assert np.all(leaves["action_truncated"] == 1)
    pool.expand_and_backup(
        leaves["leaf_id"],
        np.zeros((2, 1), dtype=np.float32),
        np.zeros(2, dtype=np.float32),
    )
    root = pool.root_policy()
    assert np.all(root["action_truncated"] == 1)


def test_self_play_pool_reuses_external_belief_buffers_without_changing_packets() -> None:
    seed = 977
    into = _pool(seed)
    reference = _pool(seed)
    buffers = _allocate_belief_buffers(into)
    addresses = {name: array.ctypes.data for name, array in buffers.items()}

    into.reset_into(buffers, seed=seed)
    request = reference.reset(seed=seed)
    _assert_same_packet(request, buffers)
    assert {name: array.ctypes.data for name, array in buffers.items()} == addresses

    into.belief_requests_into(buffers)
    _assert_same_packet(reference.belief_requests(), buffers)

    # Submit equivalent detached beliefs, finish the pending MCTS leaves, then
    # deliberately pass a strided view into the output buffer as input. The
    # native code copies those ids before overwriting the packet.
    into.submit_beliefs(buffers["state_id"], _completed_beliefs(seed, buffers))
    reference.submit_beliefs(request["state_id"], _completed_beliefs(seed, request))
    into_leaves = into.select_leaves()
    reference_leaves = reference.select_leaves()
    into.expand_and_backup(
        into_leaves["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        np.zeros(2, dtype=np.float32),
    )
    reference.expand_and_backup(
        reference_leaves["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        np.zeros(2, dtype=np.float32),
    )
    action_ids = buffers["action_id"][:, 0]
    expected = reference.step(action_ids.copy())
    into.step_into(action_ids, buffers)
    _assert_same_packet(expected, buffers)
    assert {name: array.ctypes.data for name, array in buffers.items()} == addresses


def test_self_play_belief_batch_spec_and_external_buffers_are_validated() -> None:
    pool = _pool(991)
    spec = pool.belief_batch_spec()
    request = pool.reset(seed=991)
    assert set(spec) == set(request)
    for name, field in spec.items():
        assert tuple(field["shape"]) == request[name].shape
        assert np.dtype(field["dtype"]) == request[name].dtype

    buffers = _allocate_belief_buffers(pool)
    bad = dict(buffers)
    bad["episode_id"] = bad["state_id"]
    with pytest.raises(ValueError, match="overlaps"):
        pool.belief_requests_into(bad)

    with pytest.raises(ValueError, match="int32"):
        pool.step_into(np.zeros(2, dtype=np.int64), buffers)


def test_self_play_pool_reuses_external_mcts_leaf_and_root_buffers() -> None:
    seed = 997
    into = _pool(seed)
    reference = _pool(seed)
    into_request = into.reset(seed=seed)
    reference_request = reference.reset(seed=seed)
    into.submit_beliefs(into_request["state_id"], _completed_beliefs(seed, into_request))
    reference.submit_beliefs(
        reference_request["state_id"], _completed_beliefs(seed, reference_request)
    )

    leaf_buffers = _allocate_buffers(into.leaf_batch_spec())
    leaf_addresses = {name: array.ctypes.data for name, array in leaf_buffers.items()}
    leaf_count = into.select_leaves_into(leaf_buffers)
    expected_leaves = reference.select_leaves()
    assert leaf_count == expected_leaves["leaf_id"].shape[0]
    assert set(leaf_buffers) == set(expected_leaves)
    for name, value in expected_leaves.items():
        np.testing.assert_array_equal(leaf_buffers[name][:leaf_count], value, err_msg=name)
    assert {name: array.ctypes.data for name, array in leaf_buffers.items()} == leaf_addresses

    logits = np.zeros((leaf_count, 128), dtype=np.float32)
    values = np.array([0.25, -0.5], dtype=np.float32)
    into.expand_and_backup(leaf_buffers["leaf_id"][:leaf_count], logits, values)
    reference.expand_and_backup(expected_leaves["leaf_id"], logits, values)

    root_buffers = _allocate_buffers(into.root_policy_spec())
    root_addresses = {name: array.ctypes.data for name, array in root_buffers.items()}
    into.root_policy_into(root_buffers, temperature=0.0)
    expected_root = reference.root_policy(temperature=0.0)
    assert set(root_buffers) == set(expected_root)
    for name, value in expected_root.items():
        np.testing.assert_array_equal(root_buffers[name], value, err_msg=name)
    assert {name: array.ctypes.data for name, array in root_buffers.items()} == root_addresses


def test_self_play_external_leaf_buffer_metadata_is_validated_before_selection() -> None:
    seed = 1009
    pool = _pool(seed)
    request = pool.reset(seed=seed)
    pool.submit_beliefs(request["state_id"], _completed_beliefs(seed, request))
    buffers = _allocate_buffers(pool.leaf_batch_spec())
    bad = dict(buffers)
    bad["state_id"] = bad["leaf_id"]

    with pytest.raises(ValueError, match="overlaps"):
        pool.select_leaves_into(bad)
    assert pool.pending_count == 0

    one_leaf_buffers = _allocate_buffers(pool.leaf_batch_spec(max_leaves=1))
    assert pool.select_leaves_into(one_leaf_buffers, max_leaves=1) == 1
