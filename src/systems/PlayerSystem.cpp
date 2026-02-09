//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#include "PlayerSystem.h"

#include "../game/Game.h"
#include "systems/MonumentSystem.h"

#include <algorithm>

// NOTE:
// This system mirrors UnitSystem: other systems should prefer calling PlayerSystem
// to mutate Player state (stars, kills, techs, lists), so we can keep side-effects
// and invariants in one place.
//
// The implementation assumes `Game::getPlayer(PlayerId)` exists and returns a reference.
// If your Game has different accessors, adjust `getP()` / `getPc()` below.

static inline Player& getP(Game& game, PlayerId pid) {
    return game.getPlayer(pid);
}

static inline const Player& getPc(const Game& game, PlayerId pid) {
    return game.getPlayer(pid);
}

bool PlayerSystem::playerExists(const Game& game, PlayerId pid) {
    // If you have a dedicated API (e.g. game.playerExists(pid)), use it here.
    // For now we only guard against the sentinel id.
    return pid != kNoPlayer;
}

// ---- Identity / tribe ----
PlayerId PlayerSystem::getId(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return kNoPlayer;
    return getPc(game, pid).getId();
}

TribeType PlayerSystem::getTribeType(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return TribeType::Unknown;
    return getPc(game, pid).getTribeType();
}

// ---- Stars ----
int PlayerSystem::getStars(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return 0;
    return getPc(game, pid).getStars();
}

bool PlayerSystem::spendStars(Game& game, PlayerId pid, int amount) {
    if (!playerExists(game, pid)) return false;
    if (amount <= 0) return true;

    Player& p = getP(game, pid);
    const bool ok = p.spendStars(amount);
    if (ok) {
        // Central place for star side-effects.
        MonumentSystem::onStarsUpdated(game, pid);
    }
    return ok;
}

void PlayerSystem::addStars(Game& game, PlayerId pid, int amount) {
    if (!playerExists(game, pid)) return;
    if (amount == 0) return;

    Player& p = getP(game, pid);
    p.addStars(amount);

    // Central place for star side-effects.
    MonumentSystem::onStarsUpdated(game, pid);
}

// ---- Kills ----
uint16_t PlayerSystem::getKillerCount(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return 0;
    return getPc(game, pid).getKillerCount();
}

bool PlayerSystem::addKill(Game& game, PlayerId pid, int kills) {
    if (!playerExists(game, pid)) return false;
    if (kills <= 0) return true;

    Player& p = getP(game, pid);
    const bool ok = p.addKill(kills);

    // If you have kill-based monument hooks, put them here.
    // (No call is made by default to avoid compile errors if not present.)

    return ok;
}

// ---- Pacifist (Altar of Peace) ----
uint8_t PlayerSystem::getNoAttackTurns(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return 0;
    return getPc(game, pid).getNoAttackTurns();
}

void PlayerSystem::setAttackedThisTurn(Game& game, PlayerId pid, bool v) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).setAttackedThisTurn(v);
}

bool PlayerSystem::getAttackedThisTurn(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return false;
    return getPc(game, pid).getAttackedThisTurn();
}

void PlayerSystem::resetNoAttackTurns(Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).resetNoAttackTurns();
}

void PlayerSystem::incrementNoAttackTurns(Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).incrementNoAttackTurns();
}

// ---- Tech ----
bool PlayerSystem::hasTech(const Game& game, PlayerId pid, TechId tech) {
    if (!playerExists(game, pid)) return false;
    return getPc(game, pid).hasTech(tech);
}

void PlayerSystem::addTech(Game& game, PlayerId pid, TechId tech) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).addTech(tech);
}

const std::vector<TechId>& PlayerSystem::getTechs(const Game& game, PlayerId pid) {
    static const std::vector<TechId> kEmpty;
    if (!playerExists(game, pid)) return kEmpty;
    return getPc(game, pid).getTechs();
}

bool PlayerSystem::hasAllTechUnlocked(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return false;
    return getPc(game, pid).hasAllTechUnlocked();
}

// ---- Capital ----
CityId PlayerSystem::getCapitalId(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return kNoCity;
    return getPc(game, pid).getCapitalId();
}

void PlayerSystem::setCapitalId(Game& game, PlayerId pid, CityId id) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).setCapitalId(id);
}

// ---- Cities / Units lists ----
const std::vector<CityId>& PlayerSystem::getCities(const Game& game, PlayerId pid) {
    static const std::vector<CityId> kEmpty;
    if (!playerExists(game, pid)) return kEmpty;
    return getPc(game, pid).getCities();
}

const std::vector<UnitId>& PlayerSystem::getUnits(const Game& game, PlayerId pid) {
    static const std::vector<UnitId> kEmpty;
    if (!playerExists(game, pid)) return kEmpty;
    return getPc(game, pid).getUnits();
}

void PlayerSystem::addCity(Game& game, PlayerId pid, CityId id) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).addCity(id);
}

void PlayerSystem::removeCity(Game& game, PlayerId pid, CityId id) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).removeCity(id);
}

void PlayerSystem::addUnit(Game& game, PlayerId pid, UnitId id) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).addUnit(id);
}

void PlayerSystem::removeUnit(Game& game, PlayerId pid, UnitId id) {
    if (!playerExists(game, pid)) return;
    getP(game, pid).removeUnit(id);
}

// ---- Monuments ----
std::vector<BuildingTypeEnum> PlayerSystem::getEarnedMonuments(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return {};
    return getPc(game, pid).getEarnedMonuments();
}

std::vector<BuildingTypeEnum> PlayerSystem::getPlacedMonuments(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return {};
    return getPc(game, pid).getPlacedMonuments();
}

std::vector<BuildingTypeEnum> PlayerSystem::getOwnedMonuments(const Game& game, PlayerId pid) {
    if (!playerExists(game, pid)) return {};
    return getPc(game, pid).getOwnedMonuments();
}

bool PlayerSystem::hasEarnedMonument(const Game& game, PlayerId pid, BuildingTypeEnum monument) {
    if (!playerExists(game, pid)) return false;
    return getPc(game, pid).hasEarnedMonument(monument);
}

bool PlayerSystem::hasPlacedMonument(const Game& game, PlayerId pid, BuildingTypeEnum monument) {
    if (!playerExists(game, pid)) return false;
    return getPc(game, pid).hasPlacedMonument(monument);
}

bool PlayerSystem::addMonument(Game& game, PlayerId pid, BuildingTypeEnum monument) {
    if (!playerExists(game, pid)) return false;
    return getP(game, pid).addMonument(monument);
}

bool PlayerSystem::placeMonument(Game& game, PlayerId pid, BuildingTypeEnum monument) {
    if (!playerExists(game, pid)) return false;
    return getP(game, pid).placeMonument(monument);
}

// ---- Diplomacy ----
bool PlayerSystem::hasMet(const Game& game, PlayerId pid, PlayerId other) {
    if (!playerExists(game, pid)) return false;
    return getPc(game, pid).hasMet(other);
}

bool PlayerSystem::markMet(Game& game, PlayerId pid, PlayerId other) {
    if (!playerExists(game, pid)) return false;
    return getP(game, pid).markMet(other);
}