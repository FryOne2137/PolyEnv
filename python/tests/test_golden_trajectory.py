"""Golden trajectory / snapshot regression tests.

Podejście: nagrana wcześniej deterministyczna trajektoria gry (golden data)
jest porównywana z aktualnym uruchomieniem silnika.

Jeśli jakakolwiek zmiana mechaniki gry zmieni stan gry przy tym samym seedzie
i tej samej sekwencji akcji → test padnie, wymuszając świadomą aktualizację
golden data.

Jak zaktualizować golden data po celowej zmianie mechaniki:
    python -m pytest python/tests/test_golden_trajectory.py --regenerate-golden
  lub uruchom:
    python python/tests/test_golden_trajectory.py regenerate

"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest

from game_engine import GameEnv, tribes


# ---------------------------------------------------------------------------
# Konfiguracja golden data
# ---------------------------------------------------------------------------

GOLDEN_FILE = Path(__file__).parent / "golden_trajectory_seed42.json"

SEED = 42
MAP_SIZE = 11
PLAYERS = (tribes.Bardur, tribes.Imperius)
STEPS = 30


# ---------------------------------------------------------------------------
# Generator snapshots (używany i w generacji i w testach)
# ---------------------------------------------------------------------------

def _make_env() -> GameEnv:
    return GameEnv(seed=SEED, map_size=MAP_SIZE, players=PLAYERS)


def _take_snapshot(env: GameEnv, step_i: int, chosen_action_id: int) -> dict[str, Any]:
    """Zbiera minimalny, deterministyczny snapshot stanu gry."""
    obs = env.observation()
    params = env.legal_param_actions()
    return {
        "step": step_i,
        "turn": obs["turn"],
        "current_player": obs["current_player"],
        "player_stars": obs["player_stars"],
        "own_cities": obs["own_cities"],
        "owns_units": obs["owns_units"],
        "next_turn_star_income": obs["next_turn_star_income"],
        "game_over": obs["game_over"],
        "legal_action_count": len(env.legal_action_ids_fast()),
        "action_types": sorted({a["type"] for a in params}),
        "terrain_hash": sum(t[16] * (i + 1) for i, t in enumerate(obs["tokenized_map"])),
        "chosen_action_id": int(chosen_action_id),
        "chosen_action_type": next(
            (a["type"] for a in params if a["action_id"] == chosen_action_id),
            "unknown",
        ),
    }


def generate_golden() -> dict[str, Any]:
    """Generuje golden data. Wywołaj raz, zapisz wynik do pliku."""
    env = _make_env()
    snapshots = []
    for step_i in range(STEPS):
        if env.is_done():
            break
        legal = env.legal_action_ids_fast()
        chosen = legal[0]
        snapshots.append(_take_snapshot(env, step_i, chosen))
        env.step_fast(chosen)
    return {
        "seed": SEED,
        "map_size": MAP_SIZE,
        "players": ["Bardur", "Imperius"],
        "steps": STEPS,
        "snapshots": snapshots,
    }


def load_golden() -> dict[str, Any]:
    with GOLDEN_FILE.open() as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Testy golden trajectory
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def golden_data() -> dict[str, Any]:
    if not GOLDEN_FILE.exists():
        pytest.skip(
            f"Brak pliku golden data: {GOLDEN_FILE}. "
            "Uruchom: python python/tests/test_golden_trajectory.py regenerate"
        )
    return load_golden()


class TestGoldenTrajectory:
    """Porównuje aktualny silnik ze złotą trajektorią nagraną z wcześniej zatwierdzonej wersji."""

    def test_golden_file_exists(self) -> None:
        """Golden data musi istnieć — jeśli nie, uruchom skrypt regeneracji."""
        assert GOLDEN_FILE.exists(), (
            f"Brak golden data: {GOLDEN_FILE}\n"
            "Generuj: python python/tests/test_golden_trajectory.py regenerate"
        )

    def test_turn_progression_matches_golden(self, golden_data) -> None:
        """Numer tury w każdym kroku musi zgadzać się z golden data."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            assert legal, "Brak legalnych akcji przy replikacji"
            obs = env.observation()
            assert obs["turn"] == snap["turn"], (
                f"krok {snap['step']}: tura={obs['turn']}, oczekiwano {snap['turn']}"
            )
            env.step_fast(legal[0])

    def test_current_player_sequence_matches_golden(self, golden_data) -> None:
        """Sekwencja aktywnych graczy musi być identyczna."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            actual_player = env.current_player()
            assert actual_player == snap["current_player"], (
                f"krok {snap['step']}: gracz={actual_player}, "
                f"oczekiwano {snap['current_player']}"
            )
            env.step_fast(legal[0])

    def test_star_income_sequence_matches_golden(self, golden_data) -> None:
        """Sekwencja dochodów z gwiazd musi być identyczna."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            obs = env.observation()
            assert obs["next_turn_star_income"] == snap["next_turn_star_income"], (
                f"krok {snap['step']}: income={obs['next_turn_star_income']}, "
                f"oczekiwano {snap['next_turn_star_income']}"
            )
            env.step_fast(legal[0])

    def test_legal_action_count_matches_golden(self, golden_data) -> None:
        """Liczba legalnych akcji musi być identyczna w każdym kroku."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            assert len(legal) == snap["legal_action_count"], (
                f"krok {snap['step']}: {len(legal)} akcji, "
                f"oczekiwano {snap['legal_action_count']}"
            )
            env.step_fast(legal[0])

    def test_terrain_hash_matches_golden(self, golden_data) -> None:
        """Hash terenu mapy musi być identyczny — weryfikuje generację mapy."""
        env = _make_env()
        obs = env.observation()
        actual_hash = sum(t[16] * (i + 1) for i, t in enumerate(obs["tokenized_map"]))
        expected_hash = golden_data["snapshots"][0]["terrain_hash"]
        assert actual_hash == expected_hash, (
            f"Hash terenu={actual_hash}, oczekiwano {expected_hash}. "
            "Generacja mapy dla tego seeda się zmieniła!"
        )

    def test_player_stars_sequence_matches_golden(self, golden_data) -> None:
        """Saldo gwiazd aktywnego gracza musi być identyczne w każdym kroku.
        To jest najsilniejszy test ekonomii — każda zmiana kosztów/dochodów zostanie wykryta."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            obs = env.observation()
            assert obs["player_stars"] == snap["player_stars"], (
                f"krok {snap['step']} (tura {snap['turn']}, gracz {snap['current_player']}): "
                f"stars={obs['player_stars']}, oczekiwano {snap['player_stars']}. "
                "Zmiana ekonomii? Zaktualizuj golden data jeśli zmiana jest celowa."
            )
            env.step_fast(legal[0])

    def test_city_count_sequence_matches_golden(self, golden_data) -> None:
        """Liczba własnych miast musi być identyczna — weryfikuje zakładanie miast."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            obs = env.observation()
            assert obs["own_cities"] == snap["own_cities"], (
                f"krok {snap['step']}: own_cities={obs['own_cities']}, "
                f"oczekiwano {snap['own_cities']}"
            )
            env.step_fast(legal[0])

    def test_action_types_available_match_golden(self, golden_data) -> None:
        """Typy dostępnych akcji muszą być identyczne w każdym kroku."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            params = env.legal_param_actions()
            actual_types = sorted({a["type"] for a in params})
            expected_types = snap["action_types"]
            assert actual_types == expected_types, (
                f"krok {snap['step']}: dostępne typy={actual_types}, "
                f"oczekiwano {expected_types}"
            )
            env.step_fast(legal[0])

    def test_full_trajectory_reproducible(self, golden_data) -> None:
        """PEŁNY TEST: Cała trajektoria (wszystkie pola naraz) musi być identyczna."""
        env = _make_env()
        for snap in golden_data["snapshots"]:
            legal = env.legal_action_ids_fast()
            actual = _take_snapshot(env, snap["step"], legal[0])
            # Porównaj wszystkie pola poza chosen_action_* (te są redundantne)
            fields = ["turn", "current_player", "player_stars", "own_cities",
                      "owns_units", "next_turn_star_income", "game_over",
                      "legal_action_count", "action_types"]
            mismatches = [
                f"{f}: actual={actual[f]!r} vs golden={snap[f]!r}"
                for f in fields if actual[f] != snap[f]
            ]
            assert not mismatches, (
                f"krok {snap['step']}: rozbieżności:\n  " + "\n  ".join(mismatches)
            )
            env.step_fast(legal[0])


# ---------------------------------------------------------------------------
# CLI do regeneracji golden data
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "regenerate":
        print(f"Generuję golden data: {GOLDEN_FILE}")
        data = generate_golden()
        GOLDEN_FILE.write_text(json.dumps(data, indent=2))
        print(f"Gotowe! Nagrano {len(data['snapshots'])} kroków.")
        print("\nPierwsze 5 kroków:")
        for s in data["snapshots"][:5]:
            print(
                f"  krok={s['step']}, tura={s['turn']}, "
                f"gracz={s['current_player']}, stars={s['player_stars']}, "
                f"action={s['chosen_action_type']}"
            )
    else:
        print("Użycie: python test_golden_trajectory.py regenerate")
        print(f"Golden file: {GOLDEN_FILE}")
        print(f"Istnieje: {GOLDEN_FILE.exists()}")
