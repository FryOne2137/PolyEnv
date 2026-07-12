from __future__ import annotations

from importlib.resources import files
from pathlib import Path
from typing import Any

from . import tribes
from ._game_engine import GameEnv as _GameEnv, MapType

Lakes = MapType.Lakes
Drylands = MapType.Drylands

# Feature indices in the tokenized_map tile vector (19 features total).
# Matches the layout produced by GameEnv.tokenized_map() / observation()["tokenized_map"].
_FEAT_ROAD_BRIDGE      = 7
_FEAT_BUILDING         = 8
_FEAT_SETTLEMENT_TYPE  = 11
_FEAT_RESOURCE         = 15
_FEAT_BASE_TERRAIN     = 16
_FEAT_TRIBE            = 17

from .tribes import (
    AiMo,
    Bardur,
    Hoodrick,
    Imperius,
    Kickoo,
    Luxidoor,
    Oumaji,
    Quetzali,
    SUPPORTED_TRIBES,
    Tribe,
    Vengir,
    XinXi,
    Yadakk,
    NAME_TO_TRIBE,
    Zebasi,
)


def _default_units_path() -> str:
    # Prefer package data path (works for wheel installs and editable installs).
    package_units = files("game_engine").joinpath("data", "Units.json")
    if package_units.is_file():
        return str(package_units)

    # Fallback for source checkouts where JSON lives in repository /data.
    repo_units = Path(__file__).resolve().parents[2] / "data" / "Units.json"
    if repo_units.is_file():
        return str(repo_units)

    return str(package_units)


def _normalize_players(players: list[Any] | tuple[Any, ...]) -> list[int]:
    out: list[int] = []
    for p in players:
        if isinstance(p, tribes.Tribe):
            if p not in tribes.SUPPORTED_TRIBES:
                raise ValueError(
                    f"Unsupported tribe for this release: {p.name}. "
                    "PolyEnv currently supports only the 12 regular tribes."
                )
            out.append(int(p))
            continue
        if isinstance(p, str):
            key = p.strip().lower()
            if key not in tribes.NAME_TO_TRIBE:
                raise ValueError(f"Unknown tribe name: {p}")
            out.append(int(tribes.NAME_TO_TRIBE[key]))
            continue
        if isinstance(p, int):
            if p < 1 or p > 12:
                raise ValueError(f"Tribe id out of range: {p}")
            out.append(p)
            continue
        raise TypeError(f"Unsupported player entry type: {type(p).__name__}")
    return out


def get_tribe(name: str) -> Tribe:
    key = name.strip().lower()
    if key not in NAME_TO_TRIBE:
        raise ValueError(f"Unknown tribe name: {name}")
    return NAME_TO_TRIBE[key]


def _normalize_map_type(map_type: MapType | str) -> MapType:
    if isinstance(map_type, MapType):
        return map_type
    if isinstance(map_type, str):
        normalized = map_type.strip().lower()
        if normalized == "lakes":
            return Lakes
        if normalized == "drylands":
            return Drylands
    raise ValueError("map_type must be Lakes or Drylands")


class GameEnv(_GameEnv):
    def __init__(
        self,
        seed: int = 0,
        map_size: int = 11,
        players: list[Any] | tuple[Any, ...] = (tribes.Bardur, tribes.Imperius),
        units_json_path: str | None = None,
        tribes: list[Any] | tuple[Any, ...] | None = None,
        map_type: MapType | str = Lakes,
    ) -> None:
        selected_players = tribes if tribes is not None else players
        super().__init__(
            map_size,
            _normalize_players(selected_players),
            seed,
            units_json_path or _default_units_path(),
            _normalize_map_type(map_type),
        )

    def reset(
        self,
        seed: int | None = None,
        map_size: int | None = None,
        players: list[Any] | tuple[Any, ...] | None = None,
        units_json_path: str | None = None,
        tribes: list[Any] | tuple[Any, ...] | None = None,
        map_type: MapType | str | None = None,
    ) -> dict[str, Any]:
        selected_players = tribes if tribes is not None else players
        observation = super().reset(
            map_size,
            _normalize_players(selected_players) if selected_players is not None else None,
            seed,
            units_json_path or _default_units_path(),
            _normalize_map_type(map_type) if map_type is not None else None,
        )
        return observation

    def clone(self) -> "GameEnv":
        """Return an independent Python-level clone, including replay history."""
        snapshot = super().clone()
        cloned = GameEnv()
        cloned.load_state(snapshot)
        return cloned

    def copy(self) -> "GameEnv":
        """Alias for :meth:`clone`."""
        return self.clone()

    def save(self, path: str | Path) -> Path:
        """Save this match as a deterministic ``.polygame`` action replay."""
        target = Path(path)
        super().save_replay(str(target))
        return target

    def load(self, path: str | Path) -> dict[str, Any]:
        """Load a ``.polygame`` replay and return its final player observation."""
        try:
            return super().load_replay(str(Path(path)))
        except RuntimeError as exc:
            raise ValueError(str(exc)) from exc

    def tokenized_map(
        self,
        player_id: int | None = None,
        visible_only: bool = True,
        hidden_value: int = -1,
    ) -> list[list[int]]:
        return super().tokenized_map(player_id, visible_only, hidden_value)

    def last_revealed_tiles(
        self,
        *,
        return_features: bool = False,
        perspective: int | None = None,
    ) -> "list[int] | tuple[list[int], list[list[int]]]":
        """Return tile indices revealed by the most recent step.

        The engine tracks these automatically — no snapshot needed.
        Call this at any point after a step to learn what was uncovered.

        Args:
            return_features: If ``True``, also return the real (ground-truth) feature
                             vectors for each revealed tile — analogous to
                             ``np.unique(return_counts=True)``.
            perspective:     Player whose observation mask is used.
                             Defaults to the current player.

        Returns:
            * ``list[int]`` — revealed tile indices
              (when *return_features* is ``False``)
            * ``(list[int], list[list[int]])`` — (indices, feature vectors)
              (when *return_features* is ``True``)

        Note:
            With ``step_fast_no_reveal`` the acting player's list is always
            empty — the engine intentionally withholds that information.

        Example::

            env.step_fast(action_id)

            # indices only
            revealed = env.last_revealed_tiles()

            # indices + ground-truth feature vectors (19 ints each)
            revealed, features = env.last_revealed_tiles(return_features=True)
            for idx, feat in zip(revealed, features):
                print(f"tile {idx}: terrain={feat[16]}, resource={feat[15]}")
        """
        revealed: list[int] = self._last_revealed_indices(perspective)
        if not return_features:
            return revealed
        full_map: list[list[int]] = super().tokenized_map(
            perspective, False, -1  # visible_only=False → real values
        )
        features: list[list[int]] = [full_map[i] for i in revealed]
        return revealed, features


def clone_with_predictions(
    env: "GameEnv",
    predictions: dict[int, list[int]],
    perspective: int | None = None,
) -> "GameEnv":
    """Clone *env* and apply partial tile predictions for hidden tiles.

    The clone's ``Game`` state is a deep copy; only the hidden tiles listed in
    *predictions* are overwritten.  Visible tiles in *predictions* are silently
    ignored (enforced in C++).

    Safe tile features written per entry (index → 19-int feature vector):
        [7]  roadBridge     [8]  buildingType   [11] settlementType (non-City)
        [15] resource       [16] baseTerrain    [17] tribe

    Features NOT written (require Game-level objects):
        visibility, units, city ownership, territoryCityId.

    Args:
        env:         Source environment (not mutated).
        predictions: Sparse dict ``{tile_index: feature_vector}`` — only the
                     tiles you want to predict, not the whole map.
        perspective: Player whose hidden mask is used as the guard.
                     Defaults to the current player.

    Returns:
        A new GameEnv clone with the predicted terrain applied.

    Example::

        predictions = {tile_index: feature_vector}
        cloned = clone_with_predictions(env, predictions)
        # run MCTS rollout on `cloned` …
    """
    cloned = env.clone()
    cloned._apply_tile_predictions(predictions, perspective)
    return cloned


__all__ = [
    "GameEnv",
    "MapType",
    "Lakes",
    "Drylands",
    "Tribe",
    "get_tribe",
    "NAME_TO_TRIBE",
    "SUPPORTED_TRIBES",
    "tribes",
    "clone_with_predictions",
    "XinXi",
    "Imperius",
    "Bardur",
    "Oumaji",
    "Kickoo",
    "Hoodrick",
    "Luxidoor",
    "Vengir",
    "Zebasi",
    "AiMo",
    "Quetzali",
    "Yadakk",
]
