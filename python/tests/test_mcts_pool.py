from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import Bardur, GameEnv, Imperius, MctsPool


def _roots() -> list[GameEnv]:
    return [
        GameEnv(seed=401 + index, map_size=11, players=(Bardur, Imperius))
        for index in range(2)
    ]


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
