from __future__ import annotations

from game_engine import Bardur, GameEnv, Kickoo, Tribe, get_tribe


def test_game_env_accepts_direct_tribe_constants() -> None:
    env = GameEnv(seed=2137, map_size=16, tribes=[Bardur, Kickoo])
    obs = env.observation(player_id=0, visible_only=False)

    assert isinstance(obs, dict)
    assert env.current_player() == 0


def test_get_tribe_resolves_by_name() -> None:
    assert get_tribe("Bardur") is Tribe.Bardur
    assert get_tribe("kickoo") is Tribe.Kickoo
