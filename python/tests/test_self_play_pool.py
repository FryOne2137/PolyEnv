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


def _oracle_beliefs(pool: SelfPlayPool, request: dict[str, np.ndarray]) -> np.ndarray:
    """Complete test-only beliefs from the pool's explicitly training-only labels."""
    completed = pool.belief_targets_numpy(request["state_id"]).astype(np.int32, copy=True)
    visible = request["map_tokens"][:, :, 0] == 1
    completed[:, :, 0] = request["map_tokens"][:, :, 0]
    completed[visible] = request["map_tokens"][visible]
    return completed


def _oracle_all_player_beliefs(
    seed: int,
    request: dict[str, np.ndarray],
    players: tuple = (Bardur, Imperius),
) -> np.ndarray:
    """Test-only per-player completions for the full-ISMCTS contract."""
    num_envs, player_count = request["map_tokens"].shape[:2]
    completed = np.empty(
        (num_envs, player_count, request["map_tokens"].shape[2], 23), dtype=np.int32
    )
    for env in range(num_envs):
        full = _scalar(seed + env, players).full_map_numpy().astype(np.int32, copy=True)
        for player in range(player_count):
            observed = request["map_tokens"][env, player]
            hypothesis = full.copy()
            visible = observed[:, 0] == 1
            hypothesis[:, 0] = observed[:, 0]
            hypothesis[visible] = observed[visible]
            completed[env, player] = hypothesis
    return completed


def _oracle_ismcts_particles(
    pool: SelfPlayPool,
    request: dict[str, np.ndarray],
    particles: int,
) -> np.ndarray:
    """Test-only complete [B,P,K,T,23] hypotheses for the current states."""
    assert particles > 0
    state_ids = request["state_id"][:, 0]
    full = pool.belief_targets_numpy(state_ids).astype(np.int32, copy=True)
    num_envs, player_count, tiles, features = request["map_tokens"].shape
    completed = np.empty(
        (num_envs, player_count, particles, tiles, features), dtype=np.int32
    )
    for env in range(num_envs):
        for player in range(player_count):
            observed = request["map_tokens"][env, player]
            hypothesis = full[env].copy()
            visible = observed[:, 0] == 1
            hypothesis[:, 0] = observed[:, 0]
            hypothesis[visible] = observed[visible]
            completed[env, player] = hypothesis
    return completed


def _advance_with_action(
    pool: SelfPlayPool,
    request: dict[str, np.ndarray],
    action_ids: np.ndarray,
) -> dict[str, np.ndarray]:
    """Resolve the minimum MCTS lifecycle needed before a chosen live step."""
    pool.submit_beliefs(request["state_id"], _oracle_beliefs(pool, request))
    leaves = pool.select_leaves()
    pool.expand_and_backup(
        leaves["leaf_id"],
        np.zeros((len(leaves["leaf_id"]), pool.max_actions), dtype=np.float32),
        np.zeros(len(leaves["leaf_id"]), dtype=np.float32),
    )
    return pool.step(action_ids)


def _action_row(packet: dict[str, np.ndarray], env: int, action_type: int) -> int:
    rows = np.flatnonzero(
        (packet["action_mask"][env] == 1)
        & (packet["action_features"][env, :, 0] == action_type)
    )
    assert len(rows) > 0
    return int(rows[0])


def _route_oracle_beliefs(
    pool: SelfPlayPool, request: dict[str, np.ndarray]
) -> None:
    pool.submit_beliefs(request["state_id"], _oracle_beliefs(pool, request))


def _play_aggressive_until_terminal(
    pool: SelfPlayPool,
    request: dict[str, np.ndarray],
    *,
    max_moves: int = 256,
) -> tuple[dict[str, np.ndarray], int]:
    """Small deterministic policy used only to exercise native terminal records."""
    for move in range(max_moves):
        _route_oracle_beliefs(pool, request)
        features = request["action_features"][0]
        mask = request["action_mask"][0].astype(bool)
        priorities = (
            (features[:, 0] == 8) & (features[:, 8] == 14),  # capture city
            features[:, 0] == 1,  # attack
            (features[:, 0] == 8) & (features[:, 8] == 10),  # found city
            features[:, 0] == 0,  # move
            features[:, 0] == 7,  # spawn
            features[:, 0] == 8,  # other tile actions
            features[:, 0] == 9,
            features[:, 0] == 5,
            features[:, 0] == 6,
            features[:, 0] == 4,
            features[:, 0] == 2,
            features[:, 0] == 3,  # end turn
        )
        for condition in priorities:
            rows = np.flatnonzero(mask & condition)
            if len(rows):
                break
        else:  # pragma: no cover - every non-terminal position has EndTurn
            raise AssertionError("non-terminal test position has no legal action")
        row = int(rows[(move // 3) % len(rows)])
        action = np.array([request["action_id"][0, row]], dtype=np.int32)
        request = pool.step(action)
        if request["terminated"][0]:
            return request, move + 1
    raise AssertionError("deterministic terminal policy exceeded its move budget")


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


def test_self_play_pool_can_require_every_legal_action_without_dense_truncation() -> None:
    pool = SelfPlayPool(
        num_envs=1,
        seed=431,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=1,
        # 0 derives the full stable ActionSpace capacity.
        max_actions=0,
        require_all_actions=True,
    )
    request = pool.reset(seed=431)

    assert pool.require_all_actions is True
    assert pool.max_actions == pool.action_space_size
    assert request["action_truncated"][0] == 0
    assert request["legal_action_count"][0] == request["total_legal_action_count"][0]
    count = int(request["legal_action_count"][0])
    types = request["action_features"][0, :count, 0]
    # Action ids are generated by GameStateAdapter, so this includes the
    # normal engine actions (at minimum EndTurn and currently affordable techs)
    # rather than a separate, reduced self-play action vocabulary.
    assert 3 in types  # EndTurn
    assert 4 in types  # BuyTech


def test_full_ismcts_uses_all_player_beliefs_and_redeterminizes_after_end_turn() -> None:
    seed = 439
    pool = _pool(seed, max_actions=128)
    root_request = pool.reset(seed=seed)
    player_requests = pool.all_player_belief_requests()

    player_buffers = _allocate_buffers(pool.all_player_belief_batch_spec())
    addresses = {name: value.ctypes.data for name, value in player_buffers.items()}
    pool.all_player_belief_requests_into(player_buffers)
    _assert_same_packet(player_requests, player_buffers)
    assert {name: value.ctypes.data for name, value in player_buffers.items()} == addresses

    assert player_requests["map_tokens"].shape == (2, 2, 121, 23)
    assert player_requests["state"].shape == (2, 2, 11)
    assert np.array_equal(player_requests["belief_player"], np.array([[0, 1], [0, 1]], dtype=np.int32))
    assert np.array_equal(player_requests["to_play"], np.zeros((2, 2), dtype=np.int32))
    assert np.array_equal(player_requests["state_id"][:, 0], root_request["state_id"])
    assert np.array_equal(player_requests["state_id"][:, 1], root_request["state_id"])

    beliefs = _oracle_all_player_beliefs(seed, player_requests)
    # Two particles exercise the true re-determinizing path; the 4-D shorthand
    # remains accepted as K=1 by the same submit API.
    particles = np.stack((beliefs, beliefs), axis=2)
    pool.submit_beliefs(root_request["state_id"], particles)
    root_leaves = pool.select_leaves()
    assert np.array_equal(root_leaves["to_play"], np.array([0, 0], dtype=np.int32))
    pool.expand_and_backup(
        root_leaves["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        np.zeros(2, dtype=np.float32),
    )

    # The canonical first edge is EndTurn. Selecting again materializes a
    # player-1 node and must use player 1's submitted information state, not
    # a clone of the player-0 hidden completion.
    next_leaves = pool.select_leaves()
    assert np.array_equal(next_leaves["to_play"], np.array([1, 1], dtype=np.int32))
    for env in range(2):
        np.testing.assert_array_equal(
            next_leaves["map_tokens"][env], player_requests["map_tokens"][env, 1]
        )

    bad = beliefs.copy()
    visible = np.flatnonzero(player_requests["map_tokens"][0, 1, :, 0] == 1)[0]
    bad[0, 1, visible, 19] = (int(bad[0, 1, visible, 19]) + 1) % 5
    invalid_pool = _pool(seed, max_actions=128)
    invalid_root = invalid_pool.reset(seed=seed)
    with pytest.raises(ValueError, match="revealed tiles"):
        invalid_pool.submit_beliefs(invalid_root["state_id"], bad)


def test_ismcts_acceptance_mask_recovers_only_rejected_multiplayer_slot() -> None:
    players = (Bardur, Imperius, Oumaji)
    pool = _pool(400, players=players)
    root = pool.reset(seed=400)
    request = pool.all_player_belief_requests()
    particles = _oracle_ismcts_particles(pool, request, particles=3)

    # The 4-D K=1 shorthand and the full 5-D form share the same validator.
    single_particle = np.ascontiguousarray(particles[:, :, 0])
    np.testing.assert_array_equal(
        pool.ismcts_belief_acceptance_mask(root["state_id"], single_particle),
        np.ones(2, dtype=np.uint8),
    )
    assert pool.ismcts_belief_diagnostics(root["state_id"], single_particle) == []

    visible = int(np.flatnonzero(request["map_tokens"][1, 2, :, 0] == 1)[0])
    particles[1, 2, 2, visible, 19] = (
        int(particles[1, 2, 2, visible, 19]) + 1
    ) % 5
    accepted = pool.ismcts_belief_acceptance_mask(root["state_id"], particles)
    assert accepted.dtype == np.uint8
    np.testing.assert_array_equal(accepted, np.array([1, 0], dtype=np.uint8))

    accepted_out = np.full(2, 9, dtype=np.uint8)
    address = accepted_out.ctypes.data
    assert (
        pool.ismcts_belief_acceptance_mask_into(
            root["state_id"], particles, accepted_out
        )
        is None
    )
    assert accepted_out.ctypes.data == address
    np.testing.assert_array_equal(accepted_out, accepted)

    diagnostics = pool.ismcts_belief_diagnostics(root["state_id"], particles)
    assert len(diagnostics) == 1
    assert diagnostics[0]["env_id"] == 1
    assert diagnostics[0]["player"] == 2
    assert diagnostics[0]["particle"] == 2
    assert diagnostics[0]["reason"] == "belief_build_error"

    with pytest.raises(ValueError, match=r"env=1 player=2 particle=2") as error:
        pool.submit_beliefs(root["state_id"], particles)
    assert "native worker exception" not in str(error.value)
    assert pool.search_active is False
    assert pool.pending_count == 0

    # Reset/resample only the rejected environment.  Slot 0 keeps its old
    # state id and particles, then the complete batch passes one atomic submit.
    buffers = _allocate_belief_buffers(pool)
    pool.reset_slots_into(
        np.array([1], dtype=np.int32),
        np.array([9001], dtype=np.uint32),
        np.array([[3, 4, 5]], dtype=np.int32),
        buffers,
    )
    assert buffers["state_id"][0] == root["state_id"][0]
    assert buffers["state_id"][1] != root["state_id"][1]
    refreshed = pool.all_player_belief_requests()
    replacement = _oracle_ismcts_particles(pool, refreshed, particles=3)
    particles[1] = replacement[1]
    np.testing.assert_array_equal(
        pool.ismcts_belief_acceptance_mask(buffers["state_id"], particles),
        np.ones(2, dtype=np.uint8),
    )
    pool.submit_beliefs(buffers["state_id"], particles)
    assert pool.search_active is True


def test_ismcts_build_error_reports_the_invalid_unit_fields() -> None:
    pool = _pool(407)
    root = pool.reset(seed=407)
    request = pool.all_player_belief_requests()
    particles = _oracle_ismcts_particles(pool, request, particles=2)

    active = int(root["to_play"][0])
    tile = int(
        np.flatnonzero(
            (request["map_tokens"][0, active, :, 0] == 0)
            & (particles[0, active, 1, :, 4] >= 0)
        )[0]
    )
    particles[0, active, 1, tile, 2] = 15
    particles[0, active, 1, tile, 21] = 10

    np.testing.assert_array_equal(
        pool.ismcts_belief_acceptance_mask(root["state_id"], particles),
        np.array([0, 1], dtype=np.uint8),
    )
    diagnostics = pool.ismcts_belief_diagnostics(root["state_id"], particles)
    assert len(diagnostics) == 1
    assert diagnostics[0]["env_id"] == 0
    assert diagnostics[0]["player"] == active
    assert diagnostics[0]["particle"] == 1
    assert diagnostics[0]["reason"] == "belief_build_error"
    assert diagnostics[0]["invalid_unit_tile"] == tile
    assert diagnostics[0]["unit_health"] == 15
    assert diagnostics[0]["unit_max_health"] == 10
    assert diagnostics[0]["unit_validation_code"] == 1


def test_ismcts_acceptance_mask_rejects_one_active_player_particle() -> None:
    pool = _pool(400)
    root = pool.reset(seed=400)
    request = pool.all_player_belief_requests()
    particles = _oracle_ismcts_particles(pool, request, particles=2)

    active = int(root["to_play"][0])
    source = int(
        np.flatnonzero(
            (particles[0, active, 0, :, 3] == active)
            & (particles[0, active, 0, :, 4] >= 0)
        )[0]
    )
    hidden_empty_land = np.flatnonzero(
        (request["map_tokens"][0, active, :, 0] == 0)
        & (particles[0, active, 1, :, 4] < 0)
        & np.isin(particles[0, active, 1, :, 19], (2, 3, 4))
    )
    assert len(hidden_empty_land) > 0
    destination = int(hidden_empty_land[0])
    unit_fields = [2, 3, 4, 5, 21, 22]
    particles[0, active, 1, destination, unit_fields] = particles[
        0, active, 1, source, unit_fields
    ]

    np.testing.assert_array_equal(
        pool.ismcts_belief_acceptance_mask(root["state_id"], particles),
        np.array([0, 1], dtype=np.uint8),
    )
    diagnostics = pool.ismcts_belief_diagnostics(root["state_id"], particles)
    assert len(diagnostics) == 1
    assert diagnostics[0]["env_id"] == 0
    assert diagnostics[0]["player"] == active
    assert diagnostics[0]["particle"] == 1
    assert diagnostics[0]["reason"] == "legal_action_mismatch"
    assert diagnostics[0]["extra_actions"]

    with pytest.raises(ValueError, match="ordered legal action list") as error:
        pool.submit_beliefs(root["state_id"], particles)
    assert "native worker exception" not in str(error.value)
    assert pool.search_active is False
    assert pool.pending_count == 0


def test_ismcts_acceptance_mask_rejects_false_terminal_particle() -> None:
    pool = _pool(400)
    root = pool.reset(seed=400)
    request = pool.all_player_belief_requests()
    particles = _oracle_ismcts_particles(pool, request, particles=2)
    active = int(root["to_play"][0])
    perspective = 1 - active
    belief = particles[0, perspective, 1]

    opponent_units = np.flatnonzero(
        (belief[:, 3] >= 0) & (belief[:, 3] != perspective)
    )
    belief[np.ix_(opponent_units, [2, 3, 4, 5, 21, 22])] = -1
    opponent_centers = np.flatnonzero(
        (belief[:, 14] == 2)
        & (belief[:, 16] >= 0)
        & (belief[:, 16] != perspective)
    )
    assert len(opponent_centers) > 0
    opponent_city_ids = belief[opponent_centers, 15].copy()
    for city_id in opponent_city_ids:
        belief[belief[:, 6] == city_id, 6] = -1
        belief[belief[:, 22] == city_id, 22] = -1
    for center in opponent_centers:
        belief[center, 9] = -1
        belief[center, 10:14] = -1
        belief[center, 14:18] = np.array([0, -1, -1, -1], dtype=np.int32)

    np.testing.assert_array_equal(
        pool.ismcts_belief_acceptance_mask(root["state_id"], particles),
        np.array([0, 1], dtype=np.uint8),
    )
    diagnostics = pool.ismcts_belief_diagnostics(root["state_id"], particles)
    assert len(diagnostics) == 1
    assert diagnostics[0]["env_id"] == 0
    assert diagnostics[0]["player"] == perspective
    assert diagnostics[0]["particle"] == 1
    assert diagnostics[0]["reason"] == "belief_terminal"

    with pytest.raises(ValueError, match="false terminal") as error:
        pool.submit_beliefs(root["state_id"], particles)
    assert "native worker exception" not in str(error.value)
    assert pool.search_active is False
    assert pool.pending_count == 0


def test_ismcts_acceptance_mask_rejects_contract_staleness_and_active_search() -> None:
    pool = _pool(400)
    root = pool.reset(seed=400)
    request = pool.all_player_belief_requests()
    particles = _oracle_ismcts_particles(pool, request, particles=2)

    stale = root["state_id"].copy()
    stale[0] -= np.uint64(1)
    accepted_out = np.full(2, 7, dtype=np.uint8)
    with pytest.raises(ValueError, match="stale or mismatched"):
        pool.ismcts_belief_acceptance_mask(stale, particles)
    with pytest.raises(ValueError, match="stale or mismatched"):
        pool.ismcts_belief_acceptance_mask_into(stale, particles, accepted_out)
    np.testing.assert_array_equal(accepted_out, np.full(2, 7, dtype=np.uint8))

    with pytest.raises(ValueError, match="incorrect dtype"):
        pool.ismcts_belief_acceptance_mask(
            root["state_id"].astype(np.int64), particles
        )
    with pytest.raises(ValueError, match="incorrect dtype"):
        pool.ismcts_belief_acceptance_mask(
            root["state_id"], particles.astype(np.int64)
        )
    with pytest.raises(ValueError, match="C-contiguous"):
        pool.ismcts_belief_acceptance_mask(root["state_id"], particles[..., ::-1])
    strided_ids = np.empty(4, dtype=np.uint64)[::2]
    strided_ids[:] = root["state_id"]
    with pytest.raises(ValueError, match="C-contiguous"):
        pool.ismcts_belief_acceptance_mask(strided_ids, particles)
    with pytest.raises(ValueError, match="shape"):
        pool.ismcts_belief_acceptance_mask(
            root["state_id"], np.ascontiguousarray(particles[..., :-1, :])
        )
    with pytest.raises(ValueError, match="incorrect dtype"):
        pool.ismcts_belief_acceptance_mask_into(
            root["state_id"], particles, np.empty(2, dtype=np.int32)
        )
    with pytest.raises(ValueError, match="shape"):
        pool.ismcts_belief_acceptance_mask_into(
            root["state_id"], particles, np.empty(3, dtype=np.uint8)
        )
    noncontiguous_out = np.empty(4, dtype=np.uint8)[::2]
    with pytest.raises(ValueError, match="C-contiguous"):
        pool.ismcts_belief_acceptance_mask_into(
            root["state_id"], particles, noncontiguous_out
        )

    pool.submit_beliefs(root["state_id"], particles)
    with pytest.raises(RuntimeError, match="already active"):
        pool.ismcts_belief_acceptance_mask(root["state_id"], particles)
    accepted_out.fill(6)
    with pytest.raises(RuntimeError, match="already active"):
        pool.ismcts_belief_acceptance_mask_into(
            root["state_id"], particles, accepted_out
        )
    np.testing.assert_array_equal(accepted_out, np.full(2, 6, dtype=np.uint8))


def test_self_play_visible_action_history_is_compact_and_resets_per_episode() -> None:
    seed = 451
    history = 8
    pool = SelfPlayPool(
        num_envs=2,
        seed=seed,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
        visible_action_history=history,
    )
    request = pool.reset(seed=seed)

    assert pool.visible_action_history == history
    assert tuple(pool.visible_action_history_feature_names) == (
        "event_type", "actor_player", "actor_tribe", "visibility_flags",
        "source_index", "target_index", "source_unit_type", "target_unit_type",
        "source_observed_unit_id", "target_observed_unit_id", "detail_kind", "detail_value",
    )
    assert request["visible_action_history"].shape == (2, history, 12)
    assert request["visible_action_history"].dtype == np.int32
    assert request["visible_action_history_mask"].shape == (2, history)
    assert np.all(request["visible_action_history_mask"] == 0)
    assert np.all(request["visible_action_history_length"] == 0)

    move_rows = [_action_row(request, env, 0) for env in range(2)]
    moved = _advance_with_action(
        pool,
        request,
        request["action_id"][np.arange(2), move_rows].astype(np.int32, copy=True),
    )

    # A move is a visible world action to its owner.  It is right-aligned and
    # belongs to the current actor in the next live request.
    assert np.array_equal(moved["visible_action_history_length"], np.array([1, 1], dtype=np.int32))
    assert np.array_equal(moved["visible_action_history_mask"].sum(axis=1), np.array([1, 1]))
    assert np.all(moved["visible_action_history_mask"][:, :-1] == 0)
    assert np.all(moved["visible_action_history_mask"][:, -1] == 1)
    assert np.all(moved["visible_action_history"][:, -1, 0] == 7)  # ObservedEventType::Move

    pool.reset(seed=seed)
    reset = pool.belief_requests()
    assert np.all(reset["visible_action_history_length"] == 0)
    assert np.all(reset["visible_action_history_mask"] == 0)


def test_self_play_visible_action_history_reaches_mcts_leaves_and_branches() -> None:
    seed = 463
    pool = SelfPlayPool(
        num_envs=2,
        seed=seed,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
        visible_action_history=4,
    )
    request = pool.reset(seed=seed)
    move_rows = [_action_row(request, env, 0) for env in range(2)]
    request = _advance_with_action(
        pool,
        request,
        request["action_id"][np.arange(2), move_rows].astype(np.int32, copy=True),
    )

    pool.submit_beliefs(request["state_id"], _oracle_beliefs(pool, request))
    root_leaves = pool.select_leaves()
    assert np.array_equal(root_leaves["visible_action_history_length"], np.array([1, 1], dtype=np.int32))
    np.testing.assert_array_equal(
        root_leaves["visible_action_history"], request["visible_action_history"]
    )
    assert pool.abort_search() == 2

    # A fresh root still has an empty real-game history. Prefer a simulated
    # move and verify that a child leaf receives its own compact branch token
    # without cloning a whole history vector into the node.
    branch_pool = SelfPlayPool(
        num_envs=2,
        seed=seed + 1,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
        visible_action_history=4,
    )
    branch_request = branch_pool.reset(seed=seed + 1)
    branch_pool.submit_beliefs(
        branch_request["state_id"], _oracle_beliefs(branch_pool, branch_request)
    )
    root_leaves = branch_pool.select_leaves()
    logits = np.full((2, 128), -100.0, dtype=np.float32)
    branch_rows = [_action_row(root_leaves, env, 0) for env in range(2)]
    logits[np.arange(2), branch_rows] = 100.0
    branch_pool.expand_and_backup(root_leaves["leaf_id"], logits, np.zeros(2, dtype=np.float32))
    branch_leaves = branch_pool.select_leaves()

    assert np.array_equal(branch_leaves["visible_action_history_length"], np.array([1, 1], dtype=np.int32))
    assert np.all(branch_leaves["visible_action_history_mask"][:, :-1] == 0)
    assert np.all(branch_leaves["visible_action_history_mask"][:, -1] == 1)
    assert np.all(branch_leaves["visible_action_history"][:, -1, 0] == 7)
    assert branch_pool.abort_search() == 2


def test_self_play_visible_action_history_does_not_add_hidden_opponent_moves() -> None:
    seed = 479
    pool = SelfPlayPool(
        num_envs=1,
        seed=seed,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=1,
        max_actions=128,
        auto_reset=False,
        visible_action_history=16,
    )
    player_zero = pool.reset(seed=seed)
    player_zero_visibility = player_zero["map_tokens"][0, :, 0].copy()

    # EndTurn is intentionally not a world-history token. It merely gives
    # player 1 a turn in which to make a move outside player 0's visibility.
    end_turn = _action_row(player_zero, 0, 3)
    player_one = _advance_with_action(
        pool, player_zero, player_zero["action_id"][:, end_turn].astype(np.int32, copy=True)
    )
    assert int(player_one["to_play"][0]) == 1

    candidates = np.flatnonzero(
        (player_one["action_mask"][0] == 1)
        & (player_one["action_features"][0, :, 0] == 0)
        & (player_zero_visibility[player_one["action_features"][0, :, 1]] == 0)
        & (player_zero_visibility[player_one["action_features"][0, :, 2]] == 0)
    )
    assert len(candidates) > 0
    hidden_move = int(candidates[0])
    still_player_one = _advance_with_action(
        pool, player_one, player_one["action_id"][:, hidden_move].astype(np.int32, copy=True)
    )
    assert int(still_player_one["to_play"][0]) == 1

    player_one_end_turn = _action_row(still_player_one, 0, 3)
    player_zero_again = _advance_with_action(
        pool,
        still_player_one,
        still_player_one["action_id"][:, player_one_end_turn].astype(np.int32, copy=True),
    )
    assert int(player_zero_again["to_play"][0]) == 0
    # Player 0 saw neither the opponent move nor either end turn.
    assert int(player_zero_again["visible_action_history_length"][0]) == 0
    assert int(player_zero_again["visible_action_history_mask"].sum()) == 0


def test_self_play_visible_action_history_1024_specs_and_reusable_buffers() -> None:
    pool = SelfPlayPool(
        num_envs=2,
        seed=487,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        visible_action_history=1024,
    )
    request = pool.reset(seed=487)
    assert request["visible_action_history"].shape == (2, 1024, 12)
    assert request["visible_action_history"].nbytes == 2 * 1024 * 12 * 4
    assert tuple(pool.belief_batch_spec()["visible_action_history"]["shape"]) == (2, 1024, 12)
    assert tuple(pool.leaf_batch_spec()["visible_action_history"]["shape"]) == (2, 1024, 12)

    buffers = _allocate_belief_buffers(pool)
    addresses = {name: value.ctypes.data for name, value in buffers.items()}
    pool.belief_requests_into(buffers)
    _assert_same_packet(request, buffers)
    assert {name: value.ctypes.data for name, value in buffers.items()} == addresses

    pool.submit_beliefs(request["state_id"], _oracle_beliefs(pool, request))
    leaf_buffers = _allocate_buffers(pool.leaf_batch_spec())
    leaf_addresses = {name: value.ctypes.data for name, value in leaf_buffers.items()}
    leaf_count = pool.select_leaves_into(leaf_buffers)
    assert leaf_count == 2
    assert np.all(leaf_buffers["visible_action_history_length"][:leaf_count] == 0)
    assert np.all(leaf_buffers["visible_action_history_mask"][:leaf_count] == 0)
    assert {name: value.ctypes.data for name, value in leaf_buffers.items()} == leaf_addresses
    assert pool.abort_search() == leaf_count

    with pytest.raises(ValueError, match="visible_action_history"):
        SelfPlayPool(num_envs=1, visible_action_history=-1)
    with pytest.raises(ValueError, match="visible_action_history"):
        SelfPlayPool(num_envs=1, visible_action_history=1025)


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


def test_self_play_pool_multileaf_config_cancellation_and_budgets() -> None:
    """A two-leaf pipeline must stay reusable after evaluator cancellation."""
    seed = 563
    max_nodes = 8
    max_tree_bytes = 2 * 1024 * 1024
    pool = SelfPlayPool(
        num_envs=2,
        seed=seed,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=128,
        auto_reset=False,
        max_pending_leaves_per_tree=2,
        virtual_loss=0.5,
        max_nodes_per_tree=max_nodes,
        max_tree_bytes=max_tree_bytes,
    )

    assert pool.max_pending_leaves_per_tree == 2
    assert pool.virtual_loss == pytest.approx(0.5)
    assert pool.max_nodes_per_tree == max_nodes
    assert pool.max_tree_bytes == max_tree_bytes
    leaf_spec = pool.leaf_batch_spec()
    assert tuple(leaf_spec["leaf_id"]["shape"]) == (4,)
    assert tuple(leaf_spec["state_id"]["shape"]) == (4,)

    request = pool.reset(seed=seed)
    pool.submit_beliefs(request["state_id"], _completed_beliefs(seed, request))
    initial_stats = pool.search_stats()
    assert initial_stats["max_pending_leaves_per_tree"] == 2
    assert initial_stats["max_nodes_per_tree"] == max_nodes
    assert initial_stats["max_tree_bytes"] == max_tree_bytes
    assert np.all(initial_stats["tree_node_count"] == 1)
    assert np.all(initial_stats["tree_accounted_bytes"] > 0)
    assert np.all(initial_stats["tree_accounted_bytes"] <= max_tree_bytes)

    # Roots can initially provide one leaf each.  Once those roots are
    # expanded, each tree may keep two independent evaluator requests alive.
    root_leaves = pool.select_leaves()
    assert root_leaves["leaf_id"].shape == (2,)
    assert pool.pending_count == 2
    pool.expand_and_backup(
        root_leaves["leaf_id"],
        np.zeros((2, 128), dtype=np.float32),
        np.zeros(2, dtype=np.float32),
    )
    assert pool.pending_count == 0

    leaves = pool.select_leaves()
    assert leaves["leaf_id"].shape == (4,)
    assert pool.pending_count == 4
    pending_stats = pool.search_stats()
    assert pending_stats["pending_count"] == 4
    assert np.array_equal(
        pending_stats["tree_pending_count"], np.array([2, 2], dtype=np.int32)
    )
    assert np.all(pending_stats["tree_node_count"] <= max_nodes)
    assert np.all(pending_stats["tree_accounted_bytes"] <= max_tree_bytes)

    # Cancellation is idempotent, rolls back virtual loss, and makes the
    # remaining leaves safe to abort as one group.
    cancelled_ids = leaves["leaf_id"][:2]
    assert pool.cancel_leaves(cancelled_ids) == 2
    assert pool.cancel_leaves(cancelled_ids) == 0
    assert pool.pending_count == 2
    assert pool.abort_search() == 2
    assert pool.abort_search() == 0
    assert pool.pending_count == 0
    assert np.all(pool.search_stats()["tree_pending_count"] == 0)

    with pytest.raises(ValueError, match="unknown, stale or already-expanded"):
        pool.expand_and_backup(
            leaves["leaf_id"],
            np.zeros((4, 128), dtype=np.float32),
            np.zeros(4, dtype=np.float32),
        )

    # The same active forest can immediately schedule another full B * 2
    # batch: cancelled leaf ids do not leak pending state into later work.
    retried = pool.select_leaves()
    assert retried["leaf_id"].shape == (4,)
    assert pool.abort_search() == 4
    assert pool.pending_count == 0


def test_self_play_step_checked_rejects_stale_ids_without_advancing_games() -> None:
    seed = 587
    pool = _pool(seed)
    request = pool.reset(seed=seed)
    pool.submit_beliefs(request["state_id"], _completed_beliefs(seed, request))
    before_rejection = pool.belief_requests()
    actions = request["action_id"][:, 0].copy()
    stale_ids = request["state_id"].copy()
    stale_ids[0] -= np.uint64(1)

    with pytest.raises(ValueError, match="stale or mismatched"):
        pool.step_checked(actions, stale_ids)

    # The active MCTS forest and every live position remain usable after the
    # rejected late policy result.
    _assert_same_packet(before_rejection, pool.belief_requests())
    next_request = pool.step_checked(actions, request["state_id"])
    assert np.all(next_request["action_valid"] == 1)
    assert np.all(next_request["state_id"] != request["state_id"])
    assert pool.search_active is False


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
    pool = SelfPlayPool(
        num_envs=2,
        seed=seed,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=2,
        max_actions=1,
        auto_reset=False,
        require_all_actions=False,
    )
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


def test_self_play_multi_agent_explicit_seeds_and_paired_maps() -> None:
    pool = _pool(1103)
    seeds = np.array([12001, 12001], dtype=np.uint32)
    assignments = np.array([[0, 1], [1, 0]], dtype=np.int32)
    request = pool.reset(seeds=seeds, agent_ids=assignments)

    np.testing.assert_array_equal(request["map_seed"], seeds)
    np.testing.assert_array_equal(request["agent_id"], np.array([0, 1], np.int32))
    np.testing.assert_array_equal(request["map_tokens"][0], request["map_tokens"][1])

    all_players = pool.all_player_belief_requests()
    np.testing.assert_array_equal(all_players["agent_id"], assignments)
    np.testing.assert_array_equal(
        all_players["map_seed"], np.broadcast_to(seeds[:, None], (2, 2))
    )
    for player in range(2):
        np.testing.assert_array_equal(
            all_players["map_tokens"][0, player],
            all_players["map_tokens"][1, player],
        )

    legacy = pool.reset(seed=12001)
    assert np.all(legacy["agent_id"] == 0)
    assert np.all(pool.all_player_belief_requests()["agent_id"] == 0)


def test_self_play_leaf_routing_uses_leaf_to_play_and_swaps_model_behavior() -> None:
    pool = _pool(1213)
    seeds = np.array([1213, 1214], dtype=np.uint32)

    def selected_child_players(assignments: np.ndarray) -> np.ndarray:
        request = pool.reset(seeds=seeds, agent_ids=assignments)
        _route_oracle_beliefs(pool, request)
        roots = pool.select_leaves()
        np.testing.assert_array_equal(roots["agent_id"], assignments[:, 0])

        logits = np.full((2, pool.max_actions), -100.0, dtype=np.float32)
        for row, agent_id in enumerate(roots["agent_id"]):
            # Test model 0 always ends its turn; test model 1 always moves.
            action_type = 3 if int(agent_id) == 0 else 0
            action_row = _action_row(roots, row, action_type)
            logits[row, action_row] = 100.0
        pool.expand_and_backup(roots["leaf_id"], logits, np.zeros(2, np.float32))

        children = pool.select_leaves()
        assert len(children["leaf_id"]) == 2
        expected_agents = assignments[
            children["env_id"], children["to_play"]
        ]
        np.testing.assert_array_equal(children["agent_id"], expected_agents)
        players = children["to_play"].copy()
        assert pool.abort_search() == 2
        return players

    first = selected_child_players(np.array([[0, 1], [1, 0]], dtype=np.int32))
    swapped = selected_child_players(np.array([[1, 0], [0, 1]], dtype=np.int32))
    np.testing.assert_array_equal(first, np.array([1, 0], dtype=np.int32))
    np.testing.assert_array_equal(swapped, np.array([0, 1], dtype=np.int32))


def test_self_play_reset_slots_reassigns_and_rejects_old_leaf_ids() -> None:
    pool = _pool(1301)
    assignments = np.array([[0, 1], [2, 3]], dtype=np.int32)
    request = pool.reset(
        seeds=np.array([1301, 1302], dtype=np.uint32), agent_ids=assignments
    )
    _route_oracle_beliefs(pool, request)
    stale_leaves = pool.select_leaves()

    buffers = _allocate_belief_buffers(pool)
    pool.reset_slots_into(
        np.array([1], dtype=np.int32),
        np.array([9001], dtype=np.uint32),
        np.array([[7, 8]], dtype=np.int32),
        buffers,
    )
    assert pool.search_active is False
    assert pool.pending_count == 0
    assert buffers["state_id"][0] == request["state_id"][0]
    assert buffers["state_id"][1] != request["state_id"][1]
    np.testing.assert_array_equal(buffers["map_seed"], np.array([1301, 9001], np.uint32))
    np.testing.assert_array_equal(
        pool.all_player_belief_requests()["agent_id"],
        np.array([[0, 1], [7, 8]], dtype=np.int32),
    )

    _route_oracle_beliefs(pool, buffers)
    with pytest.raises(ValueError, match="unknown, stale or already-expanded"):
        pool.expand_and_backup(
            stale_leaves["leaf_id"],
            np.zeros((2, pool.max_actions), dtype=np.float32),
            np.zeros(2, dtype=np.float32),
        )
    fresh = pool.select_leaves()
    assert len(fresh["leaf_id"]) == 2
    assert pool.abort_search() == 2

    pool.reset_slots(np.array([1], np.int32), seeds=np.array([9002], np.uint32))
    np.testing.assert_array_equal(
        pool.all_player_belief_requests()["agent_id"][1],
        np.array([7, 8], dtype=np.int32),
    )


def test_self_play_128_envs_six_agents_do_not_mix_inference_rows() -> None:
    num_envs = 128
    pool = SelfPlayPool(
        num_envs=num_envs,
        seed=1401,
        map_size=11,
        players=(Bardur, Imperius),
        num_threads=4,
        max_actions=128,
        auto_reset=False,
    )
    seeds = np.arange(1401, 1401 + num_envs, dtype=np.uint32)
    env_rows = np.arange(num_envs, dtype=np.int32)
    assignments = np.stack((env_rows % 6, (env_rows + 3) % 6), axis=1)
    request = pool.reset(seeds=seeds, agent_ids=assignments)
    np.testing.assert_array_equal(request["agent_id"], assignments[:, 0])

    _route_oracle_beliefs(pool, request)
    leaves = pool.select_leaves()
    assert len(leaves["leaf_id"]) == num_envs
    assert set(np.unique(leaves["agent_id"]).tolist()) == set(range(6))

    logits = np.empty((num_envs, pool.max_actions), dtype=np.float32)
    values = np.empty(num_envs, dtype=np.float32)
    for agent_id in np.unique(leaves["agent_id"]):
        rows = leaves["agent_id"] == agent_id
        logits[rows] = float(agent_id)
        values[rows] = float(agent_id) / 10.0
    pool.expand_and_backup(leaves["leaf_id"], logits, values)

    root = pool.root_policy()
    np.testing.assert_array_equal(root["agent_id"], assignments[:, 0])
    np.testing.assert_allclose(root["root_value"], assignments[:, 0] / 10.0)


def test_self_play_completed_record_and_auto_reset_false_stop() -> None:
    pool = SelfPlayPool(
        num_envs=1,
        seed=2,
        map_size=5,
        players=(Bardur, Imperius),
        num_threads=1,
        max_actions=0,
        auto_reset=False,
    )
    request = pool.reset(
        seeds=np.array([2], np.uint32),
        agent_ids=np.array([[5, 9]], np.int32),
    )
    episode_id = request["episode_id"].copy()
    request, moves = _play_aggressive_until_terminal(pool, request)
    assert pool.completed_count == 1

    completed = _allocate_buffers(pool.completed_batch_spec())
    addresses = {name: value.ctypes.data for name, value in completed.items()}
    assert pool.drain_completed_into(completed) == 1
    assert {name: value.ctypes.data for name, value in completed.items()} == addresses
    assert completed["env_id"][0] == 0
    assert completed["episode_id"][0] == episode_id[0]
    assert completed["map_seed"][0] == 2
    np.testing.assert_array_equal(completed["agent_id"][0], np.array([5, 9], np.int32))
    assert completed["winner_player"][0] == request["winner"][0]
    assert completed["moves"][0] == moves
    assert completed["truncated"][0] == 0
    np.testing.assert_array_equal(
        completed["terminal_values"][0], request["terminal_values"][0]
    )

    stopped_state = request["state_id"].copy()
    _route_oracle_beliefs(pool, request)
    stopped = pool.step(np.array([-1], dtype=np.int32))
    np.testing.assert_array_equal(stopped["state_id"], stopped_state)
    np.testing.assert_array_equal(stopped["episode_id"], episode_id)
    assert stopped["terminated"][0] == 1
    assert pool.completed_count == 0


def test_self_play_completed_record_survives_auto_reset() -> None:
    pool = SelfPlayPool(
        num_envs=1,
        seed=2,
        map_size=5,
        players=(Bardur, Imperius),
        num_threads=1,
        max_actions=0,
        auto_reset=True,
    )
    request = pool.reset(
        seeds=np.array([2], np.uint32),
        agent_ids=np.array([[4, 6]], np.int32),
    )
    terminal_episode = request["episode_id"][0]
    request, _ = _play_aggressive_until_terminal(pool, request)

    assert request["episode_id"][0] != terminal_episode
    assert request["agent_id"][0] == 4
    record = pool.drain_completed()
    assert len(record["env_id"]) == 1
    assert record["episode_id"][0] == terminal_episode
    assert record["map_seed"][0] == 2
    np.testing.assert_array_equal(record["agent_id"][0], np.array([4, 6], np.int32))
