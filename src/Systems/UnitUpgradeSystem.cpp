//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//

#include "UnitUpgradeSystem.h"
#include "Game.h"
#include "World/Tile.h"
#include "units/UnitFactory.h"
#include "Systems/VisionSystem.h"
#include "Systems/CitySystem.h" // provides CitySystem
#include "Systems/UnitSystem.h" // provides UnitSystem
#include "Systems/PlayerSystem.h" // provides PlayerSystem

static bool isInOwnedCityTerritory(const Game& game, PlayerId ownerId, Pos pos) {
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) {
        return false;
    }

    const Tile& t = map.at(pos);
    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) {
        return false;
    }

    const uint8_t cityOwner = CitySystem::getCityOwner(game, cid);
    return static_cast<PlayerId>(cityOwner) == ownerId;
}

// Promote unit to veteran (Polytopia-like rules)
bool UnitUpgradeSystem::becomeVeteran(Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    if (UnitSystem::isVeteran(game, unitId) || UnitSystem::hasSkill(game, unitId, UnitSkill::StaticSkill)) {
        return false;
    }

    (void)UnitSystem::setVeteran(game, unitId, true);

    const int newMaxHp = UnitSystem::getMaxHealth(game, unitId) + 5;
    (void)UnitSystem::setMaxHealth(game, unitId, newMaxHp);
    (void)UnitSystem::setHealth(game, unitId, newMaxHp);

    // No-op for fog if vision doesn't change, but safe if you later tie vision to veterancy.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}


bool UnitUpgradeSystem::canUnitBecomeVeteran(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    return (!UnitSystem::isVeteran(game, unitId) &&
            !UnitSystem::hasSkill(game, unitId, UnitSkill::StaticSkill) &&
            UnitSystem::getKillCounter(game, unitId) >= 3);
}

bool UnitUpgradeSystem::canUpgradeRaftToScout(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    if (UnitSystem::getType(game, unitId) != UnitType::Raft) {
        return false;
    }

    const PlayerId ownerId = UnitSystem::getOwnerId(game, unitId);
    // Upgrades only allowed on tiles that belong to the player's city territory
    if (!isInOwnedCityTerritory(game, ownerId, UnitSystem::getPos(game, unitId))) {
        return false;
    }

    const int kScoutUpgradeCost = std::max(0, UnitFactory::getUnitCost(UnitType::Scout));

    if (!PlayerSystem::hasTech(game, ownerId, TechId::Sailing)) {
        return false;
    }

    if (PlayerSystem::getStars(game, ownerId) < kScoutUpgradeCost) {
        return false;
    }

    return true;
}

bool UnitUpgradeSystem::canUpgradeRaftToRammer(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    if (UnitSystem::getType(game, unitId) != UnitType::Raft) {
        return false;
    }

    const PlayerId ownerId = UnitSystem::getOwnerId(game, unitId);
    // Upgrades only allowed on tiles that belong to the player's city territory
    if (!isInOwnedCityTerritory(game, ownerId, UnitSystem::getPos(game, unitId))) {
        return false;
    }

    const int kRammerUpgradeCost = std::max(0, UnitFactory::getUnitCost(UnitType::Rammer));

    // Tech requirement (keep consistent with existing style)
    if (!PlayerSystem::hasTech(game, ownerId, TechId::Ramming)) {
        return false;
    }

    // Only check affordability, do NOT modify player state
    if (PlayerSystem::getStars(game, ownerId) < kRammerUpgradeCost) {
        return false;
    }

    return true;
}

bool UnitUpgradeSystem::canUpgradeRaftToBomber(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    if (UnitSystem::getType(game, unitId) != UnitType::Raft) {
        return false;
    }

    const PlayerId ownerId = UnitSystem::getOwnerId(game, unitId);
    // Upgrades only allowed on tiles that belong to the player's city territory
    if (!isInOwnedCityTerritory(game, ownerId, UnitSystem::getPos(game, unitId))) {
        return false;
    }

    const int kBomberUpgradeCost = std::max(0, UnitFactory::getUnitCost(UnitType::Bomber));

    // Tech requirement (keep consistent with existing style)
    if (!PlayerSystem::hasTech(game, ownerId, TechId::Navigation)) {
        return false;
    }

    // Only check affordability, do NOT modify player state
    if (PlayerSystem::getStars(game, ownerId) < kBomberUpgradeCost) {
        return false;
    }

    return true;
}

bool UnitUpgradeSystem::upgradeRaftToScout(Game& game, UnitId unitId) {
    if (!canUpgradeRaftToScout(game, unitId)) {
        return false;
    }

    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    const PlayerId ownerId = UnitSystem::getOwnerId(game, unitId);
    const int cost = std::max(0, UnitFactory::getUnitCost(UnitType::Scout));
    if (!PlayerSystem::spendStars(game, ownerId, cost)) {
        return false;
    }

    // Preserve Raft HP and Max HP
    const int oldHp = UnitSystem::getHealth(game, unitId);
    const int oldMaxHp = UnitSystem::getMaxHealth(game, unitId);
    const bool poisoned = UnitSystem::isPoisoned(game, unitId);
    const bool veteran = UnitSystem::isVeteran(game, unitId);
    const int kills = UnitSystem::getKillCounter(game, unitId);
    const bool wasEmbarked = UnitSystem::isEmbarked(game, unitId);
    const UnitType baseType = UnitSystem::getEmbarkedBaseType(game, unitId);
    const bool hasMoved = UnitSystem::movedThisTurn(game, unitId);
    bool hasAttackedThisTurn = UnitSystem::attackedThisTurn(game, unitId);

    const Pos pos = UnitSystem::getPos(game, unitId);

    if (hasMoved) {
        hasAttackedThisTurn = true;
    }


    Unit upgraded = UnitFactory::create(UnitType::Scout, ownerId, pos);
    upgraded.setId(unitId);

    // Preserve Raft HP/MaxHP, but guard against invalid 0/0 states (e.g. from a buggy embark).
    const int preservedMaxHp = (oldMaxHp > 0) ? oldMaxHp : upgraded.getMaxHealth();
    const int preservedHp = (oldHp > 0) ? oldHp : preservedMaxHp;

    upgraded.setMaxHealth(preservedMaxHp);
    upgraded.setHealth(std::min(preservedHp, preservedMaxHp));

    upgraded.setVeteran(veteran);
    upgraded.setPoisoned(poisoned);
    upgraded.setKillCounter(kills);
    upgraded.setAttackedThisTurn(hasAttackedThisTurn);
    upgraded.setMovedThisTurn(hasMoved);
    if (wasEmbarked) {
        upgraded.setEmbarkedBaseType(baseType);
    }

    if (!UnitSystem::replaceUnit(game, unitId, upgraded)) {
        return false;
    }

    // Upgraded unit may have different vision range – reveal accordingly.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}

bool UnitUpgradeSystem::upgradeRaftToRammer(Game& game, UnitId unitId) {
    if (!canUpgradeRaftToRammer(game, unitId)) {
        return false;
    }

    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    const PlayerId ownerId = UnitSystem::getOwnerId(game, unitId);
    const int cost = std::max(0, UnitFactory::getUnitCost(UnitType::Rammer));
    if (!PlayerSystem::spendStars(game, ownerId, cost)) {
        return false;
    }

    // Preserve Raft HP and Max HP
    const int oldHp = UnitSystem::getHealth(game, unitId);
    const int oldMaxHp = UnitSystem::getMaxHealth(game, unitId);
    const bool poisoned = UnitSystem::isPoisoned(game, unitId);
    const bool veteran = UnitSystem::isVeteran(game, unitId);
    const int kills = UnitSystem::getKillCounter(game, unitId);
    const bool wasEmbarked = UnitSystem::isEmbarked(game, unitId);
    const UnitType baseType = UnitSystem::getEmbarkedBaseType(game, unitId);

    const Pos pos = UnitSystem::getPos(game, unitId);

    Unit upgraded = UnitFactory::create(UnitType::Rammer, ownerId, pos);
    upgraded.setId(unitId);

    // Preserve Raft HP/MaxHP, but guard against invalid 0/0 states (e.g. from a buggy embark).
    const int preservedMaxHp = (oldMaxHp > 0) ? oldMaxHp : upgraded.getMaxHealth();
    const int preservedHp = (oldHp > 0) ? oldHp : preservedMaxHp;

    upgraded.setMaxHealth(preservedMaxHp);
    upgraded.setHealth(std::min(preservedHp, preservedMaxHp));

    upgraded.setVeteran(veteran);
    upgraded.setPoisoned(poisoned);
    upgraded.setKillCounter(kills);
    if (wasEmbarked) {
        upgraded.setEmbarkedBaseType(baseType);
    }

    if (!UnitSystem::replaceUnit(game, unitId, upgraded)) {
        return false;
    }

    // Upgraded unit may have different vision range – reveal accordingly.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}

bool UnitUpgradeSystem::upgradeRaftToBomber(Game& game, UnitId unitId) {
    if (!canUpgradeRaftToBomber(game, unitId)) {
        return false;
    }

    if (!UnitSystem::unitExists(game, unitId)) {
        return false;
    }

    const PlayerId ownerId = UnitSystem::getOwnerId(game, unitId);
    const int cost = std::max(0, UnitFactory::getUnitCost(UnitType::Bomber));
    if (!PlayerSystem::spendStars(game, ownerId, cost)) {
        return false;
    }

    // Preserve Raft HP and Max HP
    const int oldHp = UnitSystem::getHealth(game, unitId);
    const int oldMaxHp = UnitSystem::getMaxHealth(game, unitId);
    const bool poisoned = UnitSystem::isPoisoned(game, unitId);
    const bool veteran = UnitSystem::isVeteran(game, unitId);
    const int kills = UnitSystem::getKillCounter(game, unitId);
    const bool wasEmbarked = UnitSystem::isEmbarked(game, unitId);
    const UnitType baseType = UnitSystem::getEmbarkedBaseType(game, unitId);

    const Pos pos = UnitSystem::getPos(game, unitId);

    Unit upgraded = UnitFactory::create(UnitType::Bomber, ownerId, pos);
    upgraded.setId(unitId);

    // Preserve Raft HP/MaxHP, but guard against invalid 0/0 states (e.g. from a buggy embark).
    const int preservedMaxHp = (oldMaxHp > 0) ? oldMaxHp : upgraded.getMaxHealth();
    const int preservedHp = (oldHp > 0) ? oldHp : preservedMaxHp;

    upgraded.setMaxHealth(preservedMaxHp);
    upgraded.setHealth(std::min(preservedHp, preservedMaxHp));

    upgraded.setVeteran(veteran);
    upgraded.setPoisoned(poisoned);
    upgraded.setKillCounter(kills);
    if (wasEmbarked) {
        upgraded.setEmbarkedBaseType(baseType);
    }

    if (!UnitSystem::replaceUnit(game, unitId, upgraded)) {
        return false;
    }

    // Upgraded unit may have different vision range – reveal accordingly.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}
