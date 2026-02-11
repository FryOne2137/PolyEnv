from __future__ import annotations

from importlib.resources import files
from pathlib import Path
from typing import Any

from . import tribes
from ._game_engine import GameEnv as _GameEnv
from .tribes import (
    AiMo,
    Aquarion,
    Bardur,
    Cymanti,
    Elyrion,
    Hoodrick,
    Imperius,
    Kickoo,
    Luxidoor,
    Oumaji,
    Polaris,
    Quetzali,
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
            out.append(int(p))
            continue
        if isinstance(p, str):
            key = p.strip().lower()
            if key not in tribes.NAME_TO_TRIBE:
                raise ValueError(f"Unknown tribe name: {p}")
            out.append(int(tribes.NAME_TO_TRIBE[key]))
            continue
        if isinstance(p, int):
            if p < 1 or p > 16:
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


class GameEnv(_GameEnv):
    def __init__(
        self,
        seed: int = 0,
        map_size: int = 11,
        players: list[Any] | tuple[Any, ...] = (tribes.Bardur, tribes.Imperius),
        units_json_path: str | None = None,
        tribes: list[Any] | tuple[Any, ...] | None = None,
    ) -> None:
        selected_players = tribes if tribes is not None else players
        super().__init__(
            map_size,
            _normalize_players(selected_players),
            seed,
            units_json_path or _default_units_path(),
        )

    def reset(
        self,
        seed: int | None = None,
        map_size: int | None = None,
        players: list[Any] | tuple[Any, ...] | None = None,
        units_json_path: str | None = None,
        tribes: list[Any] | tuple[Any, ...] | None = None,
    ) -> dict[str, Any]:
        selected_players = tribes if tribes is not None else players
        return super().reset(
            map_size,
            _normalize_players(selected_players) if selected_players is not None else None,
            seed,
            units_json_path or _default_units_path(),
        )


__all__ = [
    "GameEnv",
    "Tribe",
    "get_tribe",
    "NAME_TO_TRIBE",
    "tribes",
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
    "Aquarion",
    "Elyrion",
    "Polaris",
    "Cymanti",
]
