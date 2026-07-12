from __future__ import annotations

import json

import numpy as np
import pytest

from PolyEnv import Bardur, Drylands, GameEnv, Imperius, Kickoo


def _play_actions(env: GameEnv, count: int = 8) -> list[int]:
    played: list[int] = []
    for _ in range(count):
        legal = env.legal_action_ids_fast()
        if not legal:
            break
        action_id = int(legal[0])
        ok, done, *_ = env.step_fast(action_id)
        assert ok
        played.append(action_id)
        if done:
            break
    return played


def test_save_and_load_reconstructs_final_state(tmp_path) -> None:
    original = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))
    played = _play_actions(original)
    replay_path = original.save(tmp_path / "sample.polygame")

    payload = json.loads(replay_path.read_text(encoding="utf-8"))
    assert payload["format"] == "polyenv-game"
    assert payload["format_version"] == 3
    assert payload["ruleset"] == "polyenv-2026-07"
    assert payload["seed"] == 1234
    assert payload["map_size"] == 11
    assert payload["tribes"] == [int(Bardur), int(Imperius)]
    assert payload["map_generation"]["map_type"] == 0
    assert payload["actions"] == played

    restored = GameEnv(seed=7, map_size=16, players=(Kickoo, Bardur))
    returned_observation = restored.load(replay_path)

    assert returned_observation == restored.observation()
    assert restored.current_player() == original.current_player()
    assert restored.legal_action_ids_fast() == original.legal_action_ids_fast()
    assert np.array_equal(restored.full_map_numpy(), original.full_map_numpy())
    assert restored.observation()["map_type"] == "lakes"

    cloned = restored.clone()
    assert isinstance(cloned, GameEnv)
    clone_path = cloned.save(tmp_path / "clone.polygame")
    assert json.loads(clone_path.read_text(encoding="utf-8"))["actions"] == played


def test_replay_preserves_drylands_map_type(tmp_path) -> None:
    original = GameEnv(seed=321, map_size=11, players=(Bardur, Imperius), map_type=Drylands)
    replay_path = original.save(tmp_path / "drylands.polygame")

    payload = json.loads(replay_path.read_text(encoding="utf-8"))
    assert payload["map_generation"]["map_type"] == 1

    restored = GameEnv(seed=7, map_size=16, players=(Kickoo, Bardur))
    observation = restored.load(replay_path)
    assert observation["map_type"] == "drylands"
    assert np.array_equal(restored.full_map_numpy(), original.full_map_numpy())


def test_replay_uses_effective_seed_when_initial_seed_is_random(tmp_path) -> None:
    original = GameEnv(seed=0, map_size=11, players=(Bardur, Imperius))
    _play_actions(original, count=3)
    replay_path = original.save(tmp_path / "random-seed.polygame")
    payload = json.loads(replay_path.read_text(encoding="utf-8"))

    assert payload["seed"] != 0

    restored = GameEnv()
    restored.load(replay_path)
    assert np.array_equal(restored.full_map_numpy(), original.full_map_numpy())


def test_invalid_steps_are_not_recorded_and_invalid_replay_does_not_mutate_env(tmp_path) -> None:
    env = GameEnv(seed=1234, map_size=11, players=(Bardur, Imperius))
    before = env.full_map_numpy().copy()
    assert not env.step_fast(999_999_999)[0]

    replay_path = env.save(tmp_path / "invalid.polygame")
    payload = json.loads(replay_path.read_text(encoding="utf-8"))
    assert payload["actions"] == []
    payload["actions"] = [999_999_999]
    replay_path.write_text(json.dumps(payload), encoding="utf-8")

    with pytest.raises(ValueError, match="action at index 0 is not legal"):
        env.load(replay_path)

    assert np.array_equal(env.full_map_numpy(), before)


def test_legacy_replay_reports_ruleset_incompatibility(tmp_path) -> None:
    legacy_path = tmp_path / "legacy.polygame"
    legacy_path.write_text(
        json.dumps(
            {
                "format": "polyenv-game",
                "format_version": 1,
                "seed": 1234,
                "map_size": 11,
                "tribes": [int(Bardur), int(Imperius)],
                "actions": [],
            }
        ),
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match="legacy .polygame v1"):
        GameEnv().load(legacy_path)
