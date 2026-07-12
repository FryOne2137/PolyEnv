from __future__ import annotations

from importlib.resources import files
from pathlib import Path
from typing import Any

from . import tribes
from ._game_engine import GameEnv as _GameEnv, MapType

Lakes = MapType.Lakes
Drylands = MapType.Drylands

# Feature indices in the tokenized_map tile vector (18 features total).
# Matches the layout produced by GameEnv.tokenized_map() / observation()["tokenized_map"].
_FEAT_ROAD_BRIDGE = 7
_FEAT_BUILDING = 8
_FEAT_SETTLEMENT_TYPE = 11
_FEAT_RESOURCE = 15
_FEAT_BASE_TERRAIN = 16
_FEAT_TRIBE = 17

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
    package_units = files("PolyEnv").joinpath("data", "Units.json")
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

        The engine tracks these automatically, so no snapshot is needed.
        Call this at any point after a step to learn what was uncovered.
        """
        revealed: list[int] = self._last_revealed_indices(perspective)
        if not return_features:
            return revealed
        full_map: list[list[int]] = super().tokenized_map(perspective, False, -1)
        features: list[list[int]] = [full_map[i] for i in revealed]
        return revealed, features


def hidden_action_targets(env: "GameEnv", hidden_value: int = -1) -> set[int]:
    """Return tile indices that are both hidden and legal action targets."""
    hidden = set(env.hidden_tile_indices())
    if not hidden:
        return set()
    return {
        a["target_index"]
        for a in env.legal_param_actions()
        if a["target_index"] >= 0 and a["target_index"] in hidden
    }


def clone_with_predictions(
    env: "GameEnv",
    predictions: dict[int, list[int]],
    perspective: int | None = None,
) -> "GameEnv":
    """Clone *env* and apply partial tile predictions for hidden tiles."""
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
    "hidden_action_targets",
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
