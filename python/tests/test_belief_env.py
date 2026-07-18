from __future__ import annotations

import numpy as np
import pytest

from PolyEnv import GameEnv, tribes


def _env() -> GameEnv:
    return GameEnv(seed=123, map_size=11, players=(tribes.Bardur, tribes.Imperius))


def _completed_tokens(env: GameEnv, perspective: int = 0) -> np.ndarray:
    """Test-only complete hypothesis: truth under fog, observation on known tiles."""
    observed = env.player_map_numpy(perspective)
    completed = env.full_map_numpy().copy()
    completed[:, 0] = observed[:, 0]
    completed[observed[:, 0] == 1] = observed[observed[:, 0] == 1]
    return completed


def test_make_belief_env_accepts_numpy_and_preserves_observation() -> None:
    env = _env()
    observed = env.player_map_numpy(0)

    belief = env.make_belief_env(_completed_tokens(env))

    assert belief is not env
    assert np.array_equal(belief.player_map_numpy(0), observed)


def test_make_belief_env_rejects_visible_token_mismatch() -> None:
    env = _env()
    completed = _completed_tokens(env)
    visible_index = int(np.flatnonzero(completed[:, 0] == 1)[0])
    completed[visible_index, 19] = (int(completed[visible_index, 19]) + 1) % 5

    with pytest.raises(ValueError, match="revealed tiles differ.*tile"):
        env.make_belief_env(completed)


def test_make_belief_env_rejects_changed_visibility_mask() -> None:
    env = _env()
    completed = _completed_tokens(env)
    hidden_index = int(np.flatnonzero(completed[:, 0] == 0)[0])
    completed[hidden_index, 0] = 1

    with pytest.raises(ValueError, match="visibility mask"):
        env.make_belief_env(completed)


def test_make_belief_env_ignores_changed_hidden_source_tile() -> None:
    source_a = _env()
    source_b = source_a.clone()
    completed = _completed_tokens(source_a)
    hidden_index = int(np.flatnonzero(completed[:, 0] == 0)[0])

    altered = completed[hidden_index].copy()
    altered[19] = (int(altered[19]) + 1) % 5
    source_b.apply_tile_predictions({hidden_index: altered.tolist()})

    belief_a = source_a.make_belief_env(completed)
    belief_b = source_b.make_belief_env(completed)

    assert np.array_equal(belief_a.full_map_numpy(), belief_b.full_map_numpy())
    assert belief_a.legal_action_ids_fast() == belief_b.legal_action_ids_fast()
    assert belief_a.world_seed() == belief_b.world_seed()


def test_make_belief_env_uses_a_reproducible_synthetic_seed() -> None:
    env = _env()
    completed = _completed_tokens(env)

    first = env.make_belief_env(completed)
    second = env.make_belief_env(completed.copy())

    # This is a fixed, non-random source seed. Equality would mean the belief
    # world copied the authoritative seed rather than deriving its own one.
    assert first.world_seed() != env.world_seed()
    assert first.world_seed() == second.world_seed()

    # The Python wrapper must retain the synthetic seed too; otherwise a
    # later reset() would become a side channel for the source map seed.
    first.reset()
    assert first.world_seed() == second.world_seed()

    hidden_index = int(np.flatnonzero(completed[:, 0] == 0)[0])
    changed = completed.copy()
    changed[hidden_index, 19] = (int(changed[hidden_index, 19]) + 1) % 5
    different_hypothesis = env.make_belief_env(changed)

    assert different_hypothesis.world_seed() != first.world_seed()
