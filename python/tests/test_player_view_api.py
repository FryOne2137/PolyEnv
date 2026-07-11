from __future__ import annotations

from PolyEnv import GameEnv, tribes


def _env() -> GameEnv:
    return GameEnv(
        seed=1234,
        map_size=11,
        players=(tribes.Bardur, tribes.Imperius),
    )


def test_get_techs_returns_list_for_current_player() -> None:
    env = _env()
    techs = env.get_techs()
    assert isinstance(techs, list)
    assert all(isinstance(t, int) for t in techs)


def test_observation_contains_extended_player_view_fields() -> None:
    env = _env()
    obs = env.observation()

    assert obs["map_size"] == 11
    assert isinstance(obs["current_player"], int)
    assert isinstance(obs["game_over"], bool)
    assert isinstance(obs["winner"], int)
    assert isinstance(obs["player_stars"], int)
    assert isinstance(obs["owns_units"], int)
    assert isinstance(obs["own_cities"], int)
    assert isinstance(obs["next_turn_star_income"], int)
    assert isinstance(obs["tokenized_map"], list)
    assert isinstance(obs["techs"], list)
    assert isinstance(obs["seen_lighthouses"], list)


def test_get_actions_returns_legal_param_actions_payload() -> None:
    env = _env()
    actions = env.get_actions()
    assert isinstance(actions, list)


def test_observation_own_counts_match_tokenized_map() -> None:
    env = _env()
    perspective = env.current_player()
    obs = env.observation(player_id=perspective, visible_only=True, hidden_value=-1)
    own_units_from_map = sum(1 for tile in obs["tokenized_map"] if tile[3] == perspective)
    own_cities_from_map = {tile[12] for tile in obs["tokenized_map"] if tile[9] >= 0 and tile[12] >= 0}

    assert obs["owns_units"] == own_units_from_map
    assert obs["own_cities"] == len(own_cities_from_map)


def test_attack_actions_include_damage_preview_fields() -> None:
    env = _env()
    actions = env.legal_param_actions()
    for a in actions:
        assert "damage_dealt" in a
        assert "damage_received" in a
        assert isinstance(a["damage_dealt"], int)
        assert isinstance(a["damage_received"], int)
