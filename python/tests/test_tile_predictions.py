"""Tests for partial tile-prediction injection.

Covers:
  - hidden_tile_indices()        : C++ binding
  - apply_tile_predictions()     : C++ binding
  - last_revealed_tiles()        : Python method on GameEnv
"""
from __future__ import annotations

import pytest

from PolyEnv import GameEnv, tribes

# Feature index constants (mirror __init__.py private constants).
_FEAT_ROAD_BRIDGE     = 7
_FEAT_BUILDING        = 8
_FEAT_SETTLEMENT_TYPE = 14
_FEAT_RESOURCE        = 18
_FEAT_BASE_TERRAIN    = 19
_FEAT_TRIBE           = 20


def _env(seed: int = 42) -> GameEnv:
    return GameEnv(seed=seed, map_size=11, players=(tribes.Bardur, tribes.Imperius))


# ---------------------------------------------------------------------------
# hidden_tile_indices
# ---------------------------------------------------------------------------

class TestHiddenTileIndices:
    def test_returns_list_of_ints(self) -> None:
        env = _env()
        hidden = env.hidden_tile_indices()
        assert isinstance(hidden, list)
        assert all(isinstance(i, int) for i in hidden)

    def test_all_indices_in_range(self) -> None:
        env = _env()
        total = 11 * 11
        for idx in env.hidden_tile_indices():
            assert 0 <= idx < total

    def test_consistent_with_tokenized_map(self) -> None:
        """Tiles reported as hidden must have all non-visibility features == -1."""
        env = _env()
        hidden_set = set(env.hidden_tile_indices())
        tmap = env.tokenized_map(visible_only=True, hidden_value=-1)
        for idx, tile in enumerate(tmap):
            # baseTerrain is always 0-4 for visible tiles and -1 for hidden ones.
            if idx in hidden_set:
                assert tile[_FEAT_BASE_TERRAIN] == -1, (
                    f"tile {idx} is hidden but baseTerrain != -1"
                )
            else:
                assert tile[_FEAT_BASE_TERRAIN] != -1, (
                    f"tile {idx} is visible but baseTerrain == -1"
                )

    def test_explicit_perspective_matches_current_player(self) -> None:
        env = _env()
        pid = env.current_player()
        assert env.hidden_tile_indices() == env.hidden_tile_indices(pid)

    def test_hidden_tiles_not_empty_at_game_start(self) -> None:
        """On an 11×11 map with 2 players, most tiles are in the fog at turn 0."""
        env = _env()
        assert len(env.hidden_tile_indices()) > 0


# ---------------------------------------------------------------------------
# apply_tile_predictions
# ---------------------------------------------------------------------------

class TestApplyTilePredictions:
    def _make_prediction(self, base_terrain: int = 2) -> list[int]:
        """Minimal 23-feature vector: only baseTerrain set, rest default/0."""
        feat = [0] * 23
        feat[_FEAT_BASE_TERRAIN] = base_terrain   # Land
        feat[_FEAT_RESOURCE]     = 0              # None
        feat[_FEAT_BUILDING]     = 0              # None
        feat[_FEAT_ROAD_BRIDGE]  = 0              # None
        feat[_FEAT_TRIBE]        = 0              # Unknown
        feat[_FEAT_SETTLEMENT_TYPE] = 0           # None
        return feat

    def test_returns_patched_count(self) -> None:
        env = _env()
        hidden = env.hidden_tile_indices()
        assert len(hidden) > 0
        target = hidden[0]
        preds = {target: self._make_prediction()}
        n = env.apply_tile_predictions(preds)
        assert n == 1

    def test_patched_tile_visible_in_tokenized_map(self) -> None:
        """After patching, the tile's baseTerrain must appear in the full (non-visible) map."""
        env = _env()
        hidden = env.hidden_tile_indices()
        target = hidden[0]
        preds = {target: self._make_prediction(base_terrain=3)}  # Mountain
        env.apply_tile_predictions(preds)
        full_map = env.tokenized_map(visible_only=False, hidden_value=-1)
        assert full_map[target][_FEAT_BASE_TERRAIN] == 3

    def test_visible_tile_not_overwritten(self) -> None:
        """apply_tile_predictions must silently skip tiles already visible."""
        env = _env()
        hidden_set = set(env.hidden_tile_indices())
        full_map_before = env.tokenized_map(visible_only=False, hidden_value=-1)
        # Find a tile that IS visible.
        visible_idx = next(
            i for i in range(11 * 11) if i not in hidden_set
        )
        terrain_before = full_map_before[visible_idx][_FEAT_BASE_TERRAIN]
        # Try to patch it with a different terrain.
        other_terrain = (terrain_before + 1) % 5
        preds = {visible_idx: self._make_prediction(base_terrain=other_terrain)}
        n = env.apply_tile_predictions(preds)
        assert n == 0  # nothing patched
        full_map_after = env.tokenized_map(visible_only=False, hidden_value=-1)
        assert full_map_after[visible_idx][_FEAT_BASE_TERRAIN] == terrain_before

    def test_out_of_range_index_ignored(self) -> None:
        env = _env()
        preds = {9999: self._make_prediction()}
        n = env.apply_tile_predictions(preds)
        assert n == 0

    def test_short_feature_vector_ignored(self) -> None:
        env = _env()
        hidden = env.hidden_tile_indices()
        preds = {hidden[0]: [0] * 10}  # too short
        n = env.apply_tile_predictions(preds)
        assert n == 0

    def test_empty_predictions_dict(self) -> None:
        env = _env()
        n = env.apply_tile_predictions({})
        assert n == 0

    def test_does_not_modify_original_on_clone(self) -> None:
        """apply_tile_predictions on a clone must not touch the source env."""
        env = _env()
        hidden = env.hidden_tile_indices()
        target = hidden[0]
        full_before = env.tokenized_map(visible_only=False, hidden_value=-1)
        terrain_before = full_before[target][_FEAT_BASE_TERRAIN]

        cloned = env.clone()
        preds = {target: self._make_prediction(base_terrain=(terrain_before + 1) % 5)}
        cloned.apply_tile_predictions(preds)

        full_after = env.tokenized_map(visible_only=False, hidden_value=-1)
        assert full_after[target][_FEAT_BASE_TERRAIN] == terrain_before

    def test_city_settlement_type_not_patched(self) -> None:
        """Settlement type 2 (City) must be ignored to avoid corrupting Game state."""
        env = _env()
        hidden = env.hidden_tile_indices()
        target = hidden[0]
        full_before = env.tokenized_map(visible_only=False, hidden_value=-1)
        stype_before = full_before[target][_FEAT_SETTLEMENT_TYPE]

        feat = self._make_prediction()
        feat[_FEAT_SETTLEMENT_TYPE] = 2  # City — must be skipped
        env.apply_tile_predictions({target: feat})

        full_after = env.tokenized_map(visible_only=False, hidden_value=-1)
        # baseTerrain should have been written, but settlement type unchanged.
        assert full_after[target][_FEAT_SETTLEMENT_TYPE] == stype_before

    def test_multiple_tiles_patched(self) -> None:
        env = _env()
        hidden = env.hidden_tile_indices()
        targets = hidden[:5]
        preds = {idx: self._make_prediction(base_terrain=4) for idx in targets}
        n = env.apply_tile_predictions(preds)
        assert n == len(targets)
        full = env.tokenized_map(visible_only=False, hidden_value=-1)
        for idx in targets:
            assert full[idx][_FEAT_BASE_TERRAIN] == 4


def test_legal_actions_never_target_hidden_tiles() -> None:
    """Fog-of-war must exclude hidden tiles from every legal targeted action."""
    env = _env(seed=7)
    hidden = set(env.hidden_tile_indices())
    targets = {
        action["target_index"]
        for action in env.legal_param_actions()
        if action["target_index"] >= 0
    }
    assert targets.isdisjoint(hidden)


# ---------------------------------------------------------------------------
# last_revealed_tiles  (Python method on GameEnv)
# ---------------------------------------------------------------------------

def _find_revealing_action(env: GameEnv) -> int | None:
    """Return an action_id that reveals at least one new tile, or None."""
    hidden_before = set(env.hidden_tile_indices())
    for action_id in env.legal_action_ids_fast():
        probe = env.clone()
        probe.step_fast(action_id)
        if set(probe.hidden_tile_indices()) < hidden_before:
            return action_id
    return None


class TestLastRevealedTiles:
    def test_returns_list_without_return_features(self) -> None:
        env = _env(seed=5)
        env.step_fast(env.legal_action_ids_fast()[0])
        result = env.last_revealed_tiles()
        assert isinstance(result, list)
        assert all(isinstance(i, int) for i in result)

    def test_returns_tuple_with_return_features(self) -> None:
        env = _env(seed=5)
        env.step_fast(env.legal_action_ids_fast()[0])
        result = env.last_revealed_tiles(return_features=True)
        assert isinstance(result, tuple) and len(result) == 2
        indices, features = result
        assert isinstance(indices, list)
        assert isinstance(features, list)
        assert len(indices) == len(features)

    def test_revealed_tiles_were_hidden_before_step(self) -> None:
        """Every revealed tile must have been hidden before the step."""
        env = _env(seed=9)
        hidden_before = set(env.hidden_tile_indices())
        env.step_fast(env.legal_action_ids_fast()[0])
        for idx in env.last_revealed_tiles():
            assert idx in hidden_before

    def test_revealed_tiles_now_visible(self) -> None:
        """After the step, revealed tiles must no longer be in hidden_tile_indices."""
        env = _env(seed=11)
        env.step_fast(env.legal_action_ids_fast()[0])
        revealed_set = set(env.last_revealed_tiles())
        still_hidden = set(env.hidden_tile_indices())
        assert revealed_set.isdisjoint(still_hidden), (
            "Some revealed tiles are still reported as hidden."
        )

    def test_features_length_is_23(self) -> None:
        """Each feature vector must have exactly 23 elements."""
        env = _env(seed=13)
        env.step_fast(env.legal_action_ids_fast()[0])
        _, features = env.last_revealed_tiles(return_features=True)
        for feat in features:
            assert len(feat) == 23

    def test_features_terrain_in_valid_range(self) -> None:
        """baseTerrain (feature 16) must be 0-4 for revealed (now visible) tiles."""
        env = _env(seed=17)
        env.step_fast(env.legal_action_ids_fast()[0])
        _, features = env.last_revealed_tiles(return_features=True)
        for feat in features:
            assert 0 <= feat[_FEAT_BASE_TERRAIN] <= 4

    def test_step_fast_no_reveal_returns_empty_for_actor(self) -> None:
        """step_fast_no_reveal must not expose any tiles to the acting player."""
        env = _env(seed=21)
        actor = env.current_player()
        env.step_fast_no_reveal(env.legal_action_ids_fast()[0])
        revealed = env.last_revealed_tiles(perspective=actor)
        assert revealed == [], (
            "step_fast_no_reveal leaked tile information to the acting player."
        )

    def test_revealing_action_shows_nonempty_result(self) -> None:
        """An action known to reveal tiles must produce a non-empty revealed list."""
        env = _env(seed=34)
        action_id = _find_revealing_action(env)
        if action_id is None:
            pytest.skip("No revealing action found for this seed.")
        env.step_fast(action_id)
        revealed = env.last_revealed_tiles()
        assert len(revealed) > 0

    def test_features_match_full_tokenized_map(self) -> None:
        """Feature vectors returned by return_features must equal full-map rows."""
        env = _env(seed=37)
        env.step_fast(env.legal_action_ids_fast()[0])
        revealed, features = env.last_revealed_tiles(return_features=True)
        full_map = env.tokenized_map()
        for idx, feat in zip(revealed, features):
            assert feat == full_map[idx], (
                f"Feature mismatch for tile {idx}: got {feat}, expected {full_map[idx]}"
            )

    def test_second_step_resets_revealed_list(self) -> None:
        """Each step must report only tiles revealed by THAT step, not cumulative."""
        env = _env(seed=41)
        env.step_fast(env.legal_action_ids_fast()[0])
        after_step1 = set(env.last_revealed_tiles())
        env.step_fast(env.legal_action_ids_fast()[0])
        after_step2 = set(env.last_revealed_tiles())
        assert after_step1.isdisjoint(after_step2), (
            "last_revealed_tiles accumulated across steps instead of resetting."
        )

    def test_multiple_steps_no_double_count(self) -> None:
        """Tiles must never appear as newly revealed on two different steps."""
        env = _env(seed=41)
        seen: set[int] = set()
        for _ in range(5):
            if env.is_done():
                break
            env.step_fast(env.legal_action_ids_fast()[0])
            newly = env.last_revealed_tiles()
            overlap = seen & set(newly)
            assert not overlap, f"Tiles revealed twice: {overlap}"
            seen.update(newly)
