from __future__ import annotations

import pytest

from PolyEnv import Bardur, GameEnv, Imperius, Kickoo, Tribe, get_tribe


def test_game_env_accepts_direct_tribe_constants() -> None:
    env = GameEnv(seed=2137, map_size=16, tribes=[Bardur, Kickoo])
    obs = env.observation(player_id=0, visible_only=False)

    assert isinstance(obs, dict)
    assert env.current_player() == 0


def test_game_env_accepts_direct_tribe_constants_in_players() -> None:
    env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))

    assert env.model_request_numpy()["map_tokens"].shape == (11 * 11, 23)


def test_get_tribe_resolves_by_name() -> None:
    assert get_tribe("Bardur") is Tribe.Bardur
    assert get_tribe("kickoo") is Tribe.Kickoo


def test_special_tribes_are_not_supported_publicly() -> None:
    with pytest.raises(ValueError, match="Unsupported tribe"):
        GameEnv(seed=1, map_size=11, players=(Tribe.Aquarion, Imperius))

    with pytest.raises(ValueError, match="Unknown tribe"):
        get_tribe("Aquarion")

    with pytest.raises(ValueError, match="Tribe id out of range"):
        GameEnv(seed=1, map_size=11, players=(13, 2))


def test_polyenv_import_alias_exports_public_api() -> None:
    from PolyEnv import Bardur as PolyBardur
    from PolyEnv import GameEnv as PolyGameEnv
    from PolyEnv import Imperius as PolyImperius

    env = PolyGameEnv(seed=1234, map_size=11, players=(PolyBardur, PolyImperius))

    assert env.model_request_numpy()["map_tokens"].shape == (11 * 11, 23)
