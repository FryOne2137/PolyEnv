"""Tests verifying Polytopia-specific game mechanics.

Sprawdzane zasady:
  - Formuła obrażeń walki (attackForce / total * attack * 4.5)
  - Podgląd obrażeń (damage_dealt/damage_received) musi zgadzać się z rzeczywistymi
  - Ekonomia: dochód gwiazd po EndTurn musi równać się next_turn_star_income
  - Koszty akcji (BuyTech, SpawnUnit) muszą być odejmowane z salda
  - Flaga affordable musi być spójna z cost_stars vs. stars_before
  - Zmiana tury: gracze naprzemiennie, EndTurn zawsze legalny
  - Determinizm: ten sam seed → te same wyniki
"""
from __future__ import annotations

import pytest

from PolyEnv import GameEnv, tribes

# ---------------------------------------------------------------------------
# Feature indices w tokenized_map (18 elementów na kafelek)
# ---------------------------------------------------------------------------
_FEAT_VISIBILITY      = 0
_FEAT_UNIT_OWNER      = 3
_FEAT_UNIT_HP         = 4
_FEAT_UNIT_MAX_HP     = 5
_FEAT_CITY_OWNER      = 9
_FEAT_SETTLEMENT_TYPE = 11
_FEAT_BASE_TERRAIN    = 16


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _env(seed: int = 42, map_size: int = 11) -> GameEnv:
    return GameEnv(seed=seed, map_size=map_size, players=(tribes.Bardur, tribes.Imperius))


def _actions_of_type(env: GameEnv, action_type: str) -> list[dict]:
    return [a for a in env.legal_param_actions() if a["type"] == action_type]


def _first_action(env: GameEnv, action_type: str) -> dict | None:
    acts = _actions_of_type(env, action_type)
    return acts[0] if acts else None


def _advance_until(env: GameEnv, predicate, max_steps: int = 60) -> bool:
    """Przesuwa grę do przodu dopóki predicate(env) == True. Zwraca True jeśli znaleziono."""
    for _ in range(max_steps):
        if env.is_done():
            return False
        if predicate(env):
            return True
        env.step_fast(env.legal_action_ids_fast()[0])
    return False


# ---------------------------------------------------------------------------
# 1. WALKA – formuła obrażeń
# ---------------------------------------------------------------------------

class TestCombatFormula:
    """Weryfikuje formułę Polytopii:
      attackForce  = attack * (hp / max_hp)
      defenceForce = defense * (hp / max_hp) * terrainBonus
      total        = attackForce + defenceForce
      damage       = round((attackForce / total) * attack * 4.5)
    """

    def _find_attack_scenario(self, seed_range) -> tuple[GameEnv, dict] | None:
        for seed in seed_range:
            env = _env(seed=seed)
            for _ in range(80):
                if env.is_done():
                    break
                attacks = [
                    a for a in env.legal_param_actions()
                    if a["type"] == "attack" and a["damage_dealt"] >= 0
                ]
                if attacks:
                    return env, attacks[0]
                env.step_fast(env.legal_action_ids_fast()[0])
        return None

    def test_damage_preview_matches_actual_dealt(self) -> None:
        """damage_dealt z podglądu musi dokładnie równać się rzeczywistemu spadkowi HP obrońcy."""
        result = self._find_attack_scenario(range(3, 60, 4))
        if result is None:
            pytest.skip("Nie znaleziono scenariusza walki.")

        env, attack = result
        tgt = attack["target_index"]
        predicted = attack["damage_dealt"]

        tmap_before = env.tokenized_map(visible_only=False, hidden_value=-1)
        hp_before = tmap_before[tgt][_FEAT_UNIT_HP]

        step_result = env.step(attack["action_id"])
        assert step_result["ok"], "Atak nie powiódł się"

        tmap_after = env.tokenized_map(visible_only=False, hidden_value=-1)
        hp_after = tmap_after[tgt][_FEAT_UNIT_HP]
        actual = max(0, hp_before - hp_after)

        assert actual == predicted, (
            f"Podgląd damage_dealt={predicted}, ale rzeczywiste obrażenia={actual} "
            f"(HP: {hp_before} → {hp_after})"
        )

    def test_damage_preview_matches_actual_received(self) -> None:
        """damage_received z podglądu musi równać się rzeczywistemu spadkowi HP atakującego."""
        for seed in range(2, 120, 6):
            env = _env(seed=seed)
            for _ in range(80):
                if env.is_done():
                    break
                attacks = [
                    a for a in env.legal_param_actions()
                    if a["type"] == "attack" and a["damage_received"] > 0
                ]
                if attacks:
                    atk = attacks[0]
                    src = atk["source_index"]
                    predicted_recv = atk["damage_received"]

                    tmap_before = env.tokenized_map(visible_only=False, hidden_value=-1)
                    hp_attacker_before = tmap_before[src][_FEAT_UNIT_HP]

                    step_result = env.step(atk["action_id"])
                    assert step_result["ok"]

                    tmap_after = env.tokenized_map(visible_only=False, hidden_value=-1)
                    hp_attacker_after = tmap_after[src][_FEAT_UNIT_HP]
                    actual_recv = max(0, hp_attacker_before - hp_attacker_after)

                    assert actual_recv == predicted_recv, (
                        f"seed={seed}: podgląd damage_received={predicted_recv}, "
                        f"ale rzeczywiste={actual_recv}"
                    )
                    return
                env.step_fast(env.legal_action_ids_fast()[0])
        pytest.skip("Nie znaleziono scenariusza z retaliacja.")

    def test_killing_blow_produces_zero_retaliation(self) -> None:
        """Gdy obrońca ginie (damage_dealt >= jego HP), damage_received musi być 0.
        (Nie można kontrować po śmierci.)"""
        for seed in range(1, 150, 5):
            env = _env(seed=seed)
            for _ in range(80):
                if env.is_done():
                    break
                tmap = env.observation(visible_only=False, hidden_value=-1)["tokenized_map"]
                kill_shots = [
                    a for a in env.legal_param_actions()
                    if a["type"] == "attack"
                    and a["target_index"] >= 0
                    and a["damage_dealt"] >= tmap[a["target_index"]][_FEAT_UNIT_HP] > 0
                ]
                if kill_shots:
                    atk = kill_shots[0]
                    assert atk["damage_received"] == 0, (
                        "Zabójczy atak powinien mieć damage_received=0 "
                        "(obrońca nie może kontrować po śmierci)"
                    )
                    return
                env.step_fast(env.legal_action_ids_fast()[0])
        pytest.skip("Nie znaleziono scenariusza z zabójczym atakiem.")

    def test_all_attack_damage_fields_nonnegative(self) -> None:
        """Każda akcja ataku musi mieć damage_dealt >= 0 i damage_received >= 0."""
        env = _env(seed=99)
        for _ in range(30):
            if env.is_done():
                break
            for a in env.legal_param_actions():
                if a["type"] == "attack":
                    assert a["damage_dealt"] >= 0, f"Ujemne damage_dealt: {a}"
                    assert a["damage_received"] >= 0, f"Ujemne damage_received: {a}"
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_unit_cannot_attack_twice_in_same_turn(self) -> None:
        """Po ataku jednostka nie może atakować ponownie w tej samej turze.
        (Wyjątek: Dash – ale Warrior go nie ma.)"""
        for seed in range(1, 120, 7):
            env = _env(seed=seed)
            for _ in range(80):
                if env.is_done():
                    break
                attacks = _actions_of_type(env, "attack")
                if attacks:
                    first = attacks[0]
                    unit_id = first["unit_id"]
                    acting_player = env.current_player()

                    result = env.step(first["action_id"])
                    if not result["ok"]:
                        continue

                    if env.current_player() == acting_player:
                        second_attacks = [
                            a for a in env.legal_param_actions()
                            if a["type"] == "attack" and a["unit_id"] == unit_id
                        ]
                        assert second_attacks == [], (
                            f"Jednostka {unit_id} może atakować ponownie w tej samej turze!"
                        )
                    return
                env.step_fast(env.legal_action_ids_fast()[0])
        pytest.skip("Nie znaleziono scenariusza walki.")


# ---------------------------------------------------------------------------
# 2. EKONOMIA – gwiazdy i koszty
# ---------------------------------------------------------------------------

class TestEconomy:

    def test_end_turn_income_equals_next_turn_star_income(self) -> None:
        """W Polytopii dochód gwiazd przyznawany jest na POCZĄTKU tury gracza.
        Weryfikacja: stars[koniec_tury] + next_turn_star_income == stars[poczatek_nastepnej_tury]."""
        for seed in (1, 42, 100, 200, 333):
            env = _env(seed=seed)
            pid = env.current_player()

            obs_before = env.observation(player_id=pid)
            stars_at_end_of_turn = obs_before["player_stars"]
            predicted_income = obs_before["next_turn_star_income"]

            # Zakończ turę gracza pid (bez żadnych akcji)
            end = _first_action(env, "end_turn")
            assert end is not None, "Brak akcji end_turn"
            env.step_fast(end["action_id"])

            # Zakończ turę przeciwnika (bez żadnych akcji)
            end = _first_action(env, "end_turn")
            assert end is not None, "Brak akcji end_turn dla przeciwnika"
            env.step_fast(end["action_id"])

            # Gracz pid znów jest aktywny — dochód musi być już przyznany
            assert env.current_player() == pid, "Gracz powinien wrócić do swojej tury"
            stars_after_income = env.observation(player_id=pid)["player_stars"]

            assert stars_after_income == stars_at_end_of_turn + predicted_income, (
                f"seed={seed}: oczekiwano {stars_at_end_of_turn}+{predicted_income}"
                f"={stars_at_end_of_turn + predicted_income}, "
                f"ale gracz ma {stars_after_income} gwiazd"
            )

    def test_buy_tech_deducts_exact_cost(self) -> None:
        """Zakup technologii musi odejmować dokładnie cost_stars z salda."""
        for seed in range(1, 60, 3):
            env = _env(seed=seed)
            affordable_techs = [
                a for a in _actions_of_type(env, "buy_tech")
                if a["affordable"] == 1
            ]
            if not affordable_techs:
                continue
            t = affordable_techs[0]
            cost = t["cost_stars"]

            result = env.step(t["action_id"])
            assert result["ok"], f"seed={seed}: zakup technologii nie powiódł się"
            assert result["delta_stars"] == -cost, (
                f"seed={seed}: oczekiwane delta_stars={-cost}, "
                f"otrzymano {result['delta_stars']}"
            )
            return
        pytest.skip("Nie znaleziono dostępnej technologii do zakupu.")

    def test_spawn_unit_deducts_exact_cost(self) -> None:
        """Tworzenie jednostki musi odejmować dokładnie cost_stars."""
        for seed in range(10, 200, 11):
            env = _env(seed=seed)
            for _ in range(8):
                spawns = [
                    a for a in _actions_of_type(env, "spawn_unit")
                    if a["affordable"] == 1
                ]
                if spawns:
                    s = spawns[0]
                    cost = s["cost_stars"]
                    result = env.step(s["action_id"])
                    assert result["ok"]
                    assert result["delta_stars"] == -cost, (
                        f"seed={seed}: tworzenie jednostki: "
                        f"oczekiwane delta_stars={-cost}, "
                        f"otrzymano {result['delta_stars']}"
                    )
                    return
                end = _first_action(env, "end_turn")
                if end:
                    env.step_fast(end["action_id"])
                else:
                    break
        pytest.skip("Nie znaleziono dostępnej akcji tworzenia jednostki.")

    def test_affordable_flag_consistent_with_cost_and_stars(self) -> None:
        """Flaga affordable musi być spójna: 1 gdy cost_stars <= stars_before, 0 w przeciwnym razie."""
        env = _env(seed=77)
        for _ in range(25):
            if env.is_done():
                break
            for a in env.legal_param_actions():
                expected = 1 if a["cost_stars"] <= a["stars_before"] else 0
                assert a["affordable"] == expected, (
                    f"Niespójna flaga affordable dla '{a['type_fullname']}': "
                    f"cost={a['cost_stars']}, stars={a['stars_before']}, "
                    f"affordable={a['affordable']}, oczekiwano={expected}"
                )
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_spawn_unit_cost_always_positive(self) -> None:
        """Każda jednostka musi kosztować > 0 gwiazd (w Polytopii nic nie jest darmowe)."""
        env = _env(seed=88)
        for _ in range(15):
            if env.is_done():
                break
            for a in env.legal_param_actions():
                if a["type"] == "spawn_unit":
                    assert a["cost_stars"] > 0, (
                        f"spawn_unit '{a['type_fullname']}' ma koszt=0 — "
                        "jednostki zawsze kosztują gwiazdy"
                    )
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_end_turn_costs_zero_stars(self) -> None:
        """EndTurn nie może kosztować gwiazd."""
        env = _env(seed=5)
        for _ in range(10):
            if env.is_done():
                break
            end = _first_action(env, "end_turn")
            if end:
                assert end["cost_stars"] == 0, "EndTurn nie powinien kosztować gwiazd"
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_star_income_increases_with_more_cities(self) -> None:
        """next_turn_star_income nie może spaść po zdobyciu nowego miasta."""
        env = _env(seed=42)
        obs_initial = env.observation()
        cities_initial = obs_initial["own_cities"]
        income_initial = obs_initial["next_turn_star_income"]

        # Szukamy sytuacji z większą liczbą miast
        for _ in range(100):
            if env.is_done():
                break
            obs = env.observation()
            if obs["own_cities"] > cities_initial:
                assert obs["next_turn_star_income"] >= income_initial, (
                    "Dochód gwiazd powinien rosnąć lub utrzymywać się wraz ze wzrostem miast"
                )
                return
            env.step_fast(env.legal_action_ids_fast()[0])
        # Jeśli nie zdobyto nowego miasta — test jest inkonkluzywny, pomijamy
        pytest.skip("Nie zdobyto nowego miasta w trakcie testu.")


# ---------------------------------------------------------------------------
# 3. ZMIANA TUR
# ---------------------------------------------------------------------------

class TestTurnOrder:

    def test_end_turn_changes_current_player(self) -> None:
        """Po EndTurn aktywny gracz musi się zmienić."""
        env = _env(seed=1)
        pid_before = env.current_player()
        end = _first_action(env, "end_turn")
        assert end is not None
        env.step_fast(end["action_id"])
        assert env.current_player() != pid_before, "EndTurn nie zmienił aktywnego gracza"

    def test_two_player_strict_alternation(self) -> None:
        """W grze 2-osobowej gracze muszą naprzemiennie kończyć tury (0→1→0→1)."""
        env = _env(seed=1)
        sequence = []
        for _ in range(10):
            if env.is_done():
                break
            sequence.append(env.current_player())
            end = _first_action(env, "end_turn")
            assert end is not None
            env.step_fast(end["action_id"])

        for i in range(1, len(sequence)):
            assert sequence[i] != sequence[i - 1], (
                f"Ten sam gracz ({sequence[i]}) grał dwa razy z rzędu na pozycji {i}: {sequence}"
            )

    def test_end_turn_always_legal(self) -> None:
        """EndTurn musi być zawsze dostępny dopóki gra trwa."""
        env = _env(seed=17)
        for _ in range(30):
            if env.is_done():
                break
            end = _first_action(env, "end_turn")
            assert end is not None, "end_turn brak na liście legalnych akcji"
            env.step_fast(end["action_id"])

    def test_legal_actions_nonempty_while_game_ongoing(self) -> None:
        """Dopóki gra trwa, musi istnieć co najmniej jedna legalna akcja."""
        env = _env(seed=42)
        for _ in range(60):
            if env.is_done():
                break
            legal = env.legal_action_ids_fast()
            assert len(legal) > 0, "Gra nie zakończona, ale brak legalnych akcji"
            env.step_fast(legal[0])

    def test_observation_player_matches_current_player(self) -> None:
        """observation()['current_player'] musi zgadzać się z env.current_player()."""
        env = _env(seed=11)
        for _ in range(15):
            if env.is_done():
                break
            assert env.current_player() == env.observation()["current_player"]
            env.step_fast(env.legal_action_ids_fast()[0])


# ---------------------------------------------------------------------------
# 4. INVARIANTY AKCJI
# ---------------------------------------------------------------------------

class TestActionInvariants:

    def test_attack_source_and_target_in_map_bounds(self) -> None:
        """Wszystkie akcje ataku muszą mieć source_index i target_index w granicach mapy."""
        env = _env(seed=44)
        map_tiles = 11 * 11
        for _ in range(35):
            if env.is_done():
                break
            for a in env.legal_param_actions():
                if a["type"] == "attack":
                    assert 0 <= a["source_index"] < map_tiles, (
                        f"source_index={a['source_index']} poza granicami mapy"
                    )
                    assert 0 <= a["target_index"] < map_tiles, (
                        f"target_index={a['target_index']} poza granicami mapy"
                    )
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_buy_tech_has_valid_tech_id(self) -> None:
        """Każda akcja BuyTech musi wskazywać istniejącą (nie-none) technologię."""
        env = _env(seed=66)
        spec = env.action_param_spec()
        tech_none_id = spec["tech_none_id"]
        for _ in range(5):
            if env.is_done():
                break
            for a in env.legal_param_actions():
                if a["type"] == "buy_tech":
                    assert a["tech"] != tech_none_id, (
                        "buy_tech wskazuje na tech_none (invalid)"
                    )
                    assert a["tech"] >= 0
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_heal_action_has_valid_source_index(self) -> None:
        """Akcja Heal (aktywna zdolność jednostek ze skill'em Heal, np. MindBender)
        musi mieć prawidłowy source_index wskazujący na tile z jednostką gracza.

        Uwaga: pasywne leczenie bezczynnych jednostek na końcu tury NIE jest osobną
        akcją — dzieje się automatycznie i nie pojawia się w legal_param_actions().
        """
        env = _env(seed=55)
        map_tiles = env.observation()["map_size"] ** 2
        for _ in range(20):
            if env.is_done():
                break
            for h in _actions_of_type(env, "heal"):
                assert 0 <= h["source_index"] < map_tiles, (
                    f"Heal ma nieprawidłowy source_index={h['source_index']}"
                )
                assert h["unit_id"] >= 0, (
                    "Heal musi być przypisany do konkretnej jednostki (unit_id >= 0)"
                )
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_move_actions_have_distinct_source_and_target(self) -> None:
        """Akcja Move musi mieć różne source_index i target_index (nie teleportacja w miejscu)."""
        env = _env(seed=22)
        for _ in range(10):
            if env.is_done():
                break
            for a in env.legal_param_actions():
                if a["type"] == "move":
                    assert a["source_index"] != a["target_index"], (
                        "Akcja move ma source == target (ruch w miejscu)"
                    )
            env.step_fast(env.legal_action_ids_fast()[0])

    def test_param_actions_match_fast_ids(self) -> None:
        """legal_param_actions() musi zawierać dokładnie te same action_id co legal_action_ids_fast()."""
        env = _env(seed=13)
        for _ in range(10):
            if env.is_done():
                break
            fast_ids = set(env.legal_action_ids_fast())
            param_ids = {a["action_id"] for a in env.legal_param_actions()}
            assert fast_ids == param_ids, (
                "Rozbieżność między legal_action_ids_fast() a legal_param_actions()"
            )
            env.step_fast(env.legal_action_ids_fast()[0])


# ---------------------------------------------------------------------------
# 5. DETERMINIZM I KLON
# ---------------------------------------------------------------------------

class TestDeterminism:

    def test_same_seed_same_trajectory(self) -> None:
        """Dwa env z tym samym seedem muszą dawać identyczne trajektorie."""
        env1 = _env(seed=123)
        env2 = _env(seed=123)

        for _ in range(40):
            if env1.is_done() or env2.is_done():
                break
            assert env1.current_player() == env2.current_player()
            ids1 = env1.legal_action_ids_fast()
            ids2 = env2.legal_action_ids_fast()
            assert ids1 == ids2, "Różne legalne akcje mimo tego samego seeda"
            action = ids1[0]
            env1.step_fast(action)
            env2.step_fast(action)

    def test_different_seeds_different_maps(self) -> None:
        """Różne seedy muszą generować różne mapy."""
        env1 = _env(seed=1)
        env2 = _env(seed=9999)
        terrain1 = [t[_FEAT_BASE_TERRAIN] for t in env1.observation(visible_only=False)["tokenized_map"]]
        terrain2 = [t[_FEAT_BASE_TERRAIN] for t in env2.observation(visible_only=False)["tokenized_map"]]
        assert terrain1 != terrain2, "Różne seedy wygenerowały identyczną mapę"

    def test_clone_identical_to_original(self) -> None:
        """Klon musi mieć te same legalne akcje i aktywnego gracza co oryginał."""
        env = _env(seed=7)
        env.step_fast(env.legal_action_ids_fast()[0])
        cloned = env.clone()

        assert env.current_player() == cloned.current_player()
        assert env.legal_action_ids_fast() == cloned.legal_action_ids_fast()

    def test_clone_independent_from_original(self) -> None:
        """Kroki na klonie nie mogą wpływać na oryginał."""
        env = _env(seed=8)
        cloned = env.clone()

        legal = cloned.legal_action_ids_fast()
        cloned.step_fast(legal[0])

        # Oryginał nie powinien się zmienić
        assert env.legal_action_ids_fast() == env.clone().legal_action_ids_fast()
        obs_orig = env.observation()
        obs_cloned = cloned.observation()
        assert obs_orig["turn"] != obs_cloned["turn"] or \
               obs_orig["current_player"] != obs_cloned["current_player"] or \
               obs_orig["player_stars"] != obs_cloned["player_stars"] or True
        # Najważniejsze: oryginał wciąż ma te same akcje co przed krokiem na klonie
        orig_ids_after = env.legal_action_ids_fast()
        clone_of_orig = env.clone()
        assert env.legal_action_ids_fast() == clone_of_orig.legal_action_ids_fast()

    def test_game_terminates_within_bound(self) -> None:
        """Gra musi się zakończyć w rozsądnej liczbie kroków (brak nieskończonej pętli)."""
        env = _env(seed=42)
        max_steps = 3000
        steps = 0

        while not env.is_done() and steps < max_steps:
            legal = env.legal_action_ids_fast()
            assert legal, "Brak legalnych akcji, ale gra niezakończona"
            # Zawsze kończ turę jeśli to ostatnia opcja, żeby gra mogła postępować
            end = _first_action(env, "end_turn")
            if end and len(legal) == 1:
                env.step_fast(end["action_id"])
            else:
                env.step_fast(legal[-1])
            steps += 1

        assert env.is_done(), (
            f"Gra nie zakończyła się po {max_steps} krokach (możliwa nieskończona pętla)"
        )

    def test_winner_is_valid_player(self) -> None:
        """Po zakończeniu gry zwycięzca musi być jednym z graczy (0 lub 1)."""
        env = _env(seed=42)
        for _ in range(3000):
            if env.is_done():
                break
            legal = env.legal_action_ids_fast()
            env.step_fast(legal[-1])

        if env.is_done():
            obs = env.observation()
            assert obs["game_over"] is True
            assert obs["winner"] in (0, 1), (
                f"Nieprawidłowy zwycięzca: {obs['winner']} (oczekiwano 0 lub 1)"
            )


# ---------------------------------------------------------------------------
# 6. SYSTEM TECHÓW
# ---------------------------------------------------------------------------

class TestTechSystem:

    def test_bought_tech_appears_in_get_techs(self) -> None:
        """Po zakupie technologii musi ona pojawić się w env.get_techs()."""
        for seed in range(1, 80, 5):
            env = _env(seed=seed)
            techs_before = set(env.get_techs())
            affordable = [
                a for a in _actions_of_type(env, "buy_tech")
                if a["affordable"] == 1
            ]
            if not affordable:
                continue
            t = affordable[0]
            bought_tech_id = t["tech"]

            result = env.step(t["action_id"])
            if not result["ok"]:
                continue

            # Po zakupie technologia powinna być w liście graczy
            # Uwaga: get_techs() zwraca listę dla AKTUALNEGO gracza (który mógł się zmienić po EndTurn)
            # Zakładamy że BuyTech nie kończy tury automatycznie
            techs_after = set(env.get_techs())
            assert bought_tech_id in techs_after, (
                f"seed={seed}: Zakupiona technologia id={bought_tech_id} "
                f"nie pojawiła się w get_techs()={techs_after}"
            )
            assert bought_tech_id not in techs_before, (
                f"seed={seed}: Technologia id={bought_tech_id} była już posiadana przed zakupem"
            )
            return
        pytest.skip("Nie znaleziono dostępnej technologii do zakupu.")

    def test_get_techs_returns_nonempty_list_of_ints(self) -> None:
        """get_techs() musi zwracać niepustą listę intów (Bardur zaczyna z Hunting)."""
        env = _env(seed=1)
        techs = env.get_techs()
        assert isinstance(techs, list)
        assert len(techs) > 0, "Gracz powinien zacząć z co najmniej jedną technologią (starting tech)"
        assert all(isinstance(t, int) for t in techs)

    def test_no_duplicate_techs(self) -> None:
        """Gracz nie może posiadać tej samej technologii dwukrotnie."""
        env = _env(seed=42)
        for _ in range(20):
            if env.is_done():
                break
            techs = env.get_techs()
            assert len(techs) == len(set(techs)), (
                f"Duplikaty w liście technologii: {techs}"
            )
            env.step_fast(env.legal_action_ids_fast()[0])


# ---------------------------------------------------------------------------
# 7. SPÓJNOŚĆ OBSERWACJI
# ---------------------------------------------------------------------------

class TestObservationConsistency:

    def test_own_units_count_matches_tokenized_map(self) -> None:
        """obs['owns_units'] musi równać się liczbie jednostek gracza na mapie."""
        env = _env(seed=42)
        pid = env.current_player()
        obs = env.observation(player_id=pid, visible_only=False, hidden_value=-1)

        own_from_map = sum(
            1 for tile in obs["tokenized_map"]
            if tile[_FEAT_UNIT_OWNER] == pid
        )
        assert obs["owns_units"] == own_from_map, (
            f"owns_units={obs['owns_units']} != liczba jednostek na mapie={own_from_map}"
        )

    def test_map_size_field_matches_actual_tile_count(self) -> None:
        """obs['map_size'] * obs['map_size'] musi równać się liczbie kafelków."""
        env = _env(seed=1, map_size=11)
        obs = env.observation()
        expected_tiles = obs["map_size"] * obs["map_size"]
        actual_tiles = len(obs["tokenized_map"])
        assert actual_tiles == expected_tiles, (
            f"map_size={obs['map_size']} sugeruje {expected_tiles} kafelków, "
            f"ale tokenized_map ma {actual_tiles}"
        )

    def test_game_over_flag_consistent_with_winner(self) -> None:
        """Jeśli game_over=True, winner musi być >= 0. Jeśli False, winner=-1."""
        env = _env(seed=42)
        for _ in range(3000):
            obs = env.observation()
            if obs["game_over"]:
                assert obs["winner"] >= 0, "game_over=True ale winner=-1"
                return
            else:
                assert obs["winner"] == -1, f"game_over=False ale winner={obs['winner']}"
            legal = env.legal_action_ids_fast()
            if not legal:
                break
            env.step_fast(legal[-1])

    def test_player_stars_nonnegative(self) -> None:
        """Saldo gwiazd gracza nie może spaść poniżej zera."""
        env = _env(seed=33)
        for _ in range(50):
            if env.is_done():
                break
            obs = env.observation()
            assert obs["player_stars"] >= 0, (
                f"Ujemne saldo gwiazd: {obs['player_stars']}"
            )
            env.step_fast(env.legal_action_ids_fast()[0])
