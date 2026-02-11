from __future__ import annotations

from typing import Any

from game_engine import GameEnv, tribes


TILE_FEATURE_KEYS = (
    "terrain",
    "resources",
    "buildings",
    "settlements",
    "territory_owner",
    "unit_owner",
    "unit_type",
    "unit_hp",
)


def _new_env(seed: int = 7) -> GameEnv:
    return GameEnv(
        seed=seed,
        map_size=11,
        players=(tribes.Bardur, tribes.Imperius),
    )


def _find_revealing_action(env: GameEnv, hidden_value: int) -> tuple[int, int]:
    actor = env.current_player()
    before = env.observation(player_id=actor, visible_only=True, hidden_value=hidden_value)
    before_vis = before["visibility"]

    for action_id in env.legal_action_ids_fast():
        probe = env.clone()
        ok, *_ = probe.step_fast_no_reveal(action_id)
        if not ok:
            continue
        after_vis = probe.observation(player_id=actor, visible_only=True, hidden_value=hidden_value)["visibility"]
        for idx, (v0, v1) in enumerate(zip(before_vis, after_vis)):
            if v0 == 0 and v1 == 1:
                return action_id, idx

    raise AssertionError("Could not find an action that reveals a new tile for the active player.")


def _mechanics_snapshot(env: GameEnv) -> dict[str, Any]:
    return {
        "full_obs": env.observation(player_id=0, visible_only=False, hidden_value=-1),
        "legal_ids": env.legal_action_ids_fast(),
        "done": env.is_done(),
        "current_player": env.current_player(),
    }


def test_step_fast_no_reveal_keeps_newly_visible_tile_hidden_in_visible_observation() -> None:
    env = _new_env(seed=13)
    hidden_value = -7
    action_id, new_idx = _find_revealing_action(env, hidden_value)

    actor = env.current_player()
    ok, *_ = env.step_fast_no_reveal(action_id)
    assert ok is True

    visible_obs = env.observation(player_id=actor, visible_only=True, hidden_value=hidden_value)
    full_obs = env.observation(player_id=actor, visible_only=False, hidden_value=hidden_value)

    assert visible_obs["visibility"][new_idx] == 1
    for key in TILE_FEATURE_KEYS:
        assert visible_obs[key][new_idx] == hidden_value
    assert full_obs["terrain"][new_idx] != hidden_value


def test_step_fast_no_reveal_matches_step_fast_game_mechanics() -> None:
    env_fast = _new_env(seed=21)
    env_no_reveal = _new_env(seed=21)

    for _ in range(25):
        assert env_fast.legal_action_ids_fast() == env_no_reveal.legal_action_ids_fast()
        legal = env_fast.legal_action_ids_fast()
        if not legal:
            break

        action_id = legal[0]
        out_fast = env_fast.step_fast(action_id)
        out_no_reveal = env_no_reveal.step_fast_no_reveal(action_id)
        assert out_fast == out_no_reveal
        assert _mechanics_snapshot(env_fast) == _mechanics_snapshot(env_no_reveal)

        if out_fast[1]:
            break


def test_clone_preserves_state_and_yields_identical_trajectories() -> None:
    env = _new_env(seed=34)
    action_id, _ = _find_revealing_action(env, hidden_value=-9)
    env.step_fast_no_reveal(action_id)

    cloned = env.clone()
    assert _mechanics_snapshot(env) == _mechanics_snapshot(cloned)
    assert env.observation(player_id=0, visible_only=True, hidden_value=-9) == cloned.observation(
        player_id=0, visible_only=True, hidden_value=-9
    )

    for _ in range(20):
        legal = env.legal_action_ids_fast()
        assert legal == cloned.legal_action_ids_fast()
        if not legal:
            break

        chosen = legal[min(2, len(legal) - 1)]
        assert env.step_fast_no_reveal(chosen) == cloned.step_fast_no_reveal(chosen)
        assert _mechanics_snapshot(env) == _mechanics_snapshot(cloned)

        if env.is_done():
            break
