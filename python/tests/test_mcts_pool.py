from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import Bardur, GameEnv, Imperius, MctsPool, Oumaji


def _roots() -> list[GameEnv]:
    return [
        GameEnv(seed=401 + index, map_size=11, players=(Bardur, Imperius))
        for index in range(2)
    ]


def _three_player_roots() -> list[GameEnv]:
    return [
        GameEnv(seed=913 + index, map_size=11, players=(Bardur, Imperius, Oumaji))
        for index in range(2)
    ]


def _allocate_buffers(spec: dict[str, dict[str, object]]) -> dict[str, np.ndarray]:
    return {
        name: np.empty(tuple(field["shape"]), dtype=field["dtype"])
        for name, field in spec.items()
    }


def _assert_same_prefix(
    expected: dict[str, np.ndarray], buffers: dict[str, np.ndarray], rows: int
) -> None:
    assert rows == next(iter(expected.values())).shape[0]
    assert set(buffers) == set(expected)
    for name, value in expected.items():
        np.testing.assert_array_equal(buffers[name][:rows], value, err_msg=name)


def test_mcts_pool_batches_independent_roots_and_preserves_sources() -> None:
    roots = _roots()
    source_maps = [root.player_map_numpy().copy() for root in roots]
    pool = MctsPool(roots, num_threads=2, max_actions=128, c_puct=1.5)

    batch = pool.select_leaves()

    assert pool.num_trees == 2
    assert pool.max_actions == 128
    assert pool.pending_count == 2
    assert batch["map_tokens"].shape == (2, 121, 23)
    assert batch["state"].shape == (2, 11)
    assert batch["action_id"].shape == (2, 128)
    assert batch["leaf_id"].dtype == np.uint64
    assert np.array_equal(batch["tree_id"], np.array([0, 1], dtype=np.int32))
    assert np.array_equal(batch["to_play"], np.array([0, 0], dtype=np.int32))

    for index, root in enumerate(roots):
        legal = np.asarray(root.legal_action_ids_fast(), dtype=np.int32)
        count = int(batch["legal_action_count"][index])
        assert np.array_equal(batch["map_tokens"][index], source_maps[index])
        assert count == len(legal)
        assert np.array_equal(batch["action_id"][index, :count], legal)
        assert np.all(batch["action_mask"][index, :count] == 1)
        assert np.all(batch["action_mask"][index, count:] == 0)

    # One tree admits only one outstanding neural evaluation at a time. This
    # avoids duplicate leaf expansion without Python-side virtual-loss logic.
    waiting = pool.select_leaves()
    assert waiting["leaf_id"].shape == (0,)
    assert waiting["map_tokens"].shape == (0, 121, 23)

    # Search state is a native copy; no MCTS expansion may mutate the caller's
    # live game objects.
    for root, source_map in zip(roots, source_maps):
        assert np.array_equal(root.player_map_numpy(), source_map)


def test_mcts_pool_expands_backs_up_and_returns_root_visit_policy() -> None:
    roots = _roots()
    source_maps = [root.player_map_numpy().copy() for root in roots]
    pool = MctsPool(roots, num_threads=2, max_actions=128)
    initial = pool.select_leaves()

    logits = np.zeros((2, 128), dtype=np.float32)
    values = np.array([0.25, -0.50], dtype=np.float32)
    pool.expand_and_backup(initial["leaf_id"], logits, values)

    assert pool.pending_count == 0
    first_policy = pool.root_policy()
    assert np.array_equal(first_policy["root_visit_count"], np.array([1, 1], dtype=np.int32))
    assert np.allclose(first_policy["root_value"], values)
    assert np.all(first_policy["action_mask"].sum(axis=1) > 0)
    assert np.allclose(first_policy["policy"].sum(axis=1), 1.0)

    # The next selection lazy-materializes exactly one PUCT edge per tree,
    # then the batched evaluator result is backed up through that edge.
    second = pool.select_leaves()
    assert np.all(second["leaf_id"] != initial["leaf_id"])
    assert np.array_equal(second["to_play"], np.array([1, 1], dtype=np.int32))
    opponent_values = np.array([0.40, -0.20], dtype=np.float32)
    pool.expand_and_backup(
        second["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        opponent_values,
    )

    result = pool.root_policy(temperature=0.0)
    assert np.array_equal(result["root_visit_count"], np.array([2, 2], dtype=np.int32))
    assert np.array_equal(result["visit_count"].sum(axis=1), np.array([1, 1], dtype=np.int64))
    assert np.allclose(result["policy"].sum(axis=1), 1.0)
    assert np.all(result["policy"].max(axis=1) == 1.0)
    # Values returned for the opponent must be negated at the root.
    assert np.allclose(result["root_value"], np.array([-0.075, -0.15], dtype=np.float32))

    for root, source_map in zip(roots, source_maps):
        assert np.array_equal(root.player_map_numpy(), source_map)


def test_mcts_pool_validates_evaluator_shapes_values_and_stale_leaf_ids() -> None:
    pool = MctsPool(GameEnv(seed=777, players=(Bardur, Imperius)), num_trees=2, max_actions=128)
    batch = pool.select_leaves()

    with pytest.raises(ValueError, match="policy_logits"):
        pool.expand_and_backup(
            batch["leaf_id"],
            np.zeros((2, 127), dtype=np.float32),
            np.zeros(2, dtype=np.float32),
        )
    assert pool.pending_count == 2

    with pytest.raises(ValueError, match=r"\[-1, 1\]"):
        pool.expand_and_backup(
            batch["leaf_id"],
            np.zeros((2, 128), dtype=np.float32),
            np.array([0.0, 1.5], dtype=np.float32),
        )
    assert pool.pending_count == 2

    pool.expand_and_backup(
        batch["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        np.zeros(2, dtype=np.float32),
    )
    with pytest.raises(ValueError, match="stale"):
        pool.expand_and_backup(
            batch["leaf_id"],
            np.zeros((2, 128), dtype=np.float32),
            np.zeros(2, dtype=np.float32),
        )


def test_mcts_pool_uses_max_n_value_vectors_for_three_players() -> None:
    pool = MctsPool(_three_player_roots(), num_threads=2, max_actions=128)
    initial = pool.select_leaves()

    assert pool.player_count == 3
    assert pool.num_players == 3
    assert np.array_equal(initial["player_count"], np.array([3, 3], dtype=np.int32))
    assert np.array_equal(initial["to_play"], np.array([0, 0], dtype=np.int32))

    first_values = np.array(
        [[0.20, -0.10, -0.10], [-0.40, 0.60, -0.20]], dtype=np.float32
    )
    pool.expand_and_backup(
        initial["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        first_values,
    )
    first_root = pool.root_policy()
    assert np.array_equal(first_root["player_count"], np.array([3, 3], dtype=np.int32))
    # root_value remains backward-compatible: it is the root player's
    # component, not an implicit reduction of the full value vector.
    assert np.allclose(first_root["root_value"], first_values[:, 0])

    # The deterministic equal-prior choice is EndTurn at the initial root,
    # so the next leaf belongs to player 1. Its vector must still add player
    # 0's component at the root (not negate player 1's scalar value).
    second = pool.select_leaves()
    assert np.array_equal(second["to_play"], np.array([1, 1], dtype=np.int32))
    second_values = np.array(
        [[0.80, -0.30, -0.50], [0.10, 0.70, -0.80]], dtype=np.float32
    )
    pool.expand_and_backup(
        second["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        second_values,
    )
    result = pool.root_policy()
    assert np.allclose(result["root_value"], (first_values[:, 0] + second_values[:, 0]) / 2.0)


def test_mcts_pool_rejects_non_vector_values_for_three_players() -> None:
    pool = MctsPool(_three_player_roots(), num_threads=2, max_actions=128)
    leaves = pool.select_leaves()
    logits = np.zeros((2, 128), dtype=np.float32)

    with pytest.raises(ValueError, match="player_count"):
        pool.expand_and_backup(leaves["leaf_id"], logits, np.zeros(2, dtype=np.float32))
    with pytest.raises(ValueError, match="player_count"):
        pool.expand_and_backup(leaves["leaf_id"], logits, np.zeros((2, 2), dtype=np.float32))
    with pytest.raises(ValueError, match=r"\[-1, 1\]"):
        pool.expand_and_backup(
            leaves["leaf_id"],
            logits,
            np.array([[0.0, np.nan, 0.0], [0.0, 0.0, 0.0]], dtype=np.float32),
        )

    pool.expand_and_backup(leaves["leaf_id"], logits, np.zeros((2, 3), dtype=np.float32))


def test_mcts_pool_rejects_roots_with_different_player_counts() -> None:
    with pytest.raises(ValueError, match="same player_count"):
        MctsPool(
            [
                GameEnv(seed=1001, players=(Bardur, Imperius)),
                GameEnv(seed=1002, players=(Bardur, Imperius, Oumaji)),
            ],
            max_actions=128,
        )


def test_mcts_pool_truncates_wide_action_rows_consistently() -> None:
    root = GameEnv(seed=1201, players=(Bardur, Imperius))
    pool = MctsPool(root, num_trees=1, max_actions=1)

    leaves = pool.select_leaves()
    assert np.array_equal(leaves["legal_action_count"], np.array([1], dtype=np.int32))
    assert int(leaves["total_legal_action_count"][0]) > 1
    assert np.array_equal(leaves["action_truncated"], np.array([1], dtype=np.uint8))

    end_turn = next(
        action["action_id"]
        for action in root.legal_param_actions()
        if action["type_fullname"] == "end_turn"
    )
    assert int(leaves["action_id"][0, 0]) == end_turn

    pool.expand_and_backup(
        leaves["leaf_id"],
        np.zeros((1, 1), dtype=np.float32),
        np.zeros(1, dtype=np.float32),
    )
    policy = pool.root_policy()
    assert int(policy["total_legal_action_count"][0]) > 1
    assert int(policy["action_truncated"][0]) == 1
    assert int(policy["action_id"][0, 0]) == end_turn


def test_player_count_is_limited_by_the_visibility_representation() -> None:
    with pytest.raises(ValueError, match="between 2 and 16"):
        GameEnv(seed=1101, players=(Bardur,))
    with pytest.raises(ValueError, match="between 2 and 16"):
        GameEnv(seed=1102, players=(Bardur,) * 17)


def test_mcts_pool_reuses_external_leaf_and_root_buffers() -> None:
    into = MctsPool(_roots(), num_threads=2, max_actions=128)
    reference = MctsPool(_roots(), num_threads=2, max_actions=128)
    leaf_buffers = _allocate_buffers(into.leaf_batch_spec())
    leaf_addresses = {name: array.ctypes.data for name, array in leaf_buffers.items()}

    leaf_count = into.select_leaves_into(leaf_buffers)
    expected_leaves = reference.select_leaves()
    _assert_same_prefix(expected_leaves, leaf_buffers, leaf_count)
    assert {name: array.ctypes.data for name, array in leaf_buffers.items()} == leaf_addresses
    # All trees are now pending, so a reusable capacity-two slot returns an
    # empty valid prefix without allocating a zero-row NumPy batch.
    assert into.select_leaves_into(leaf_buffers) == 0

    values = np.array([0.25, -0.5], dtype=np.float32)
    logits = np.zeros((leaf_count, 128), dtype=np.float32)
    into.expand_and_backup(leaf_buffers["leaf_id"][:leaf_count], logits, values)
    reference.expand_and_backup(expected_leaves["leaf_id"], logits, values)

    root_buffers = _allocate_buffers(into.root_policy_spec())
    root_addresses = {name: array.ctypes.data for name, array in root_buffers.items()}
    into.root_policy_into(root_buffers, temperature=0.0)
    expected_root = reference.root_policy(temperature=0.0)
    _assert_same_prefix(expected_root, root_buffers, into.num_trees)
    assert {name: array.ctypes.data for name, array in root_buffers.items()} == root_addresses


def test_mcts_pool_external_leaf_buffers_are_checked_before_pending_selection() -> None:
    pool = MctsPool(_roots(), num_threads=2, max_actions=128)
    buffers = _allocate_buffers(pool.leaf_batch_spec())

    bad_dtype = dict(buffers)
    bad_dtype["leaf_id"] = np.empty(2, dtype=np.int64)
    with pytest.raises(ValueError, match="incorrect dtype"):
        pool.select_leaves_into(bad_dtype)
    assert pool.pending_count == 0

    overlapping = dict(buffers)
    overlapping["tree_id"] = overlapping["to_play"]
    with pytest.raises(ValueError, match="overlaps"):
        pool.select_leaves_into(overlapping)
    assert pool.pending_count == 0

    one_leaf_buffers = _allocate_buffers(pool.leaf_batch_spec(max_leaves=1))
    assert pool.select_leaves_into(one_leaf_buffers, max_leaves=1) == 1
    assert pool.pending_count == 1
