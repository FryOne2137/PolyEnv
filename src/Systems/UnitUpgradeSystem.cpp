//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//

#include "UnitUpgradeSystem.h"
#include "Game.h"
#include "World/Tile.h"
#include "units/UnitFactory.h"
#include "Systems/VisionSystem.h"

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

    const City* c = game.getCity(cid);
    if (!c) {
        return false;
    }

    return static_cast<PlayerId>(c->getOwnerId()) == ownerId;
}
#include "Unit.h"
#include "units/UnitFactory.h"

// Promote unit to veteran (Polytopia-like rules)
bool UnitUpgradeSystem::becomeVeteran(Game& game, UnitId unitId) {
    Unit* u = game.getUnit(unitId);
    if (!u) {
        return false;
    }

    if (u->isVeteran() || u->hasSkill(UnitSkill::StaticSkill)) {
        return false;
    }

    u->setVeteran(true);

    const int newMaxHp = u->getMaxHealth() + 5;
    u->setMaxHealth(newMaxHp);
    u->setHealth(newMaxHp);

    // No-op for fog if vision doesn't change, but safe if you later tie vision to veterancy.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}


bool UnitUpgradeSystem::canUnitBecomeVeteran(const Game& game, UnitId unitId) {
    const Unit *u = game.getUnit(unitId);
    if (!u) {
        return false;
    }
    return (!u->isVeteran() && !u->hasSkill(UnitSkill::StaticSkill) && u->getKillCounter() >= 3);
}

bool UnitUpgradeSystem::canUpgradeRaftToScout(const Game& game, UnitId unitId) {
    const Unit* u = game.getUnit(unitId);
    if (!u) {
        return false;
    }

    if (u->getType() != UnitType::Raft) {
        return false;
    }

    const PlayerId ownerId = u->getOwnerId();
    // Upgrades only allowed on tiles that belong to the player's city territory
    if (!isInOwnedCityTerritory(game, ownerId, u->getPos())) {
        return false;
    }
    const Player& owner = game.getPlayer(ownerId);

    const int kScoutUpgradeCost = std::max(0, UnitFactory::getUnitCost(UnitType::Scout));

    if (!owner.hasTech(TechId::Sailing)) {
        return false;
    }

    if (owner.getStars() < kScoutUpgradeCost) {
        return false;
    }

    return true;
}

bool UnitUpgradeSystem::canUpgradeRaftToRammer(const Game& game, UnitId unitId) {
    const Unit* u = game.getUnit(unitId);
    if (!u) {
        return false;
    }

    if (u->getType() != UnitType::Raft) {
        return false;
    }

    const PlayerId ownerId = u->getOwnerId();
    // Upgrades only allowed on tiles that belong to the player's city territory
    if (!isInOwnedCityTerritory(game, ownerId, u->getPos())) {
        return false;
    }
    const Player& owner = game.getPlayer(ownerId);

    const int kRammerUpgradeCost = std::max(0, UnitFactory::getUnitCost(UnitType::Rammer));

    // Tech requirement (keep consistent with existing style)
    if (!owner.hasTech(TechId::Ramming)) {
        return false;
    }

    // Only check affordability, do NOT modify player state
    if (owner.getStars() < kRammerUpgradeCost) {
        return false;
    }

    return true;
}

bool UnitUpgradeSystem::canUpgradeRaftToBomber(const Game& game, UnitId unitId) {
    const Unit* u = game.getUnit(unitId);
    if (!u) {
        return false;
    }

    if (u->getType() != UnitType::Raft) {
        return false;
    }

    const PlayerId ownerId = u->getOwnerId();
    // Upgrades only allowed on tiles that belong to the player's city territory
    if (!isInOwnedCityTerritory(game, ownerId, u->getPos())) {
        return false;
    }
    const Player& owner = game.getPlayer(ownerId);

    const int kBomberUpgradeCost = std::max(0, UnitFactory::getUnitCost(UnitType::Bomber));

    // Tech requirement (keep consistent with existing style)
    if (!owner.hasTech(TechId::Navigation)) {
        return false;
    }

    // Only check affordability, do NOT modify player state
    if (owner.getStars() < kBomberUpgradeCost) {
        return false;
    }

    return true;
}

bool UnitUpgradeSystem::upgradeRaftToScout(Game& game, UnitId unitId) {
    if (!canUpgradeRaftToScout(game, unitId)) {
        return false;
    }

    Unit* oldUnit = game.getUnit(unitId);
    if (!oldUnit) {
        return false;
    }

    Player& owner = game.getPlayer(oldUnit->getOwnerId());
    const int cost = std::max(0, UnitFactory::getUnitCost(UnitType::Scout));
    if (!owner.spendStars(cost)) {
        return false;
    }

    // Preserve Raft HP and Max HP
    const int oldHp = oldUnit->getHealth();
    const int oldMaxHp = oldUnit->getMaxHealth();
    const bool poisoned = oldUnit->poisoned();
    const bool veteran = oldUnit->isVeteran();
    const int kills = oldUnit->getKillCounter();
    const bool wasEmbarked = oldUnit->isEmbarked();
    const UnitType baseType = oldUnit->getEmbarkedBaseType();
    const bool hasMoved= oldUnit->movedThisTurn();
    bool hasAttackedThisTurn= oldUnit->attackedThisTurn();

    const PlayerId ownerId = oldUnit->getOwnerId();
    const Pos pos = oldUnit->getPos();

    if (hasMoved) {
        hasAttackedThisTurn=true;
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

    *oldUnit = upgraded;

    // Upgraded unit may have different vision range – reveal accordingly.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}

bool UnitUpgradeSystem::upgradeRaftToRammer(Game& game, UnitId unitId) {
    if (!canUpgradeRaftToRammer(game, unitId)) {
        return false;
    }

    Unit* oldUnit = game.getUnit(unitId);
    if (!oldUnit) {
        return false;
    }

    Player& owner = game.getPlayer(oldUnit->getOwnerId());
    const int cost = std::max(0, UnitFactory::getUnitCost(UnitType::Rammer));
    if (!owner.spendStars(cost)) {
        return false;
    }

    // Preserve Raft HP and Max HP
    const int oldHp = oldUnit->getHealth();
    const int oldMaxHp = oldUnit->getMaxHealth();
    const bool poisoned = oldUnit->poisoned();
    const bool veteran = oldUnit->isVeteran();
    const int kills = oldUnit->getKillCounter();
    const bool wasEmbarked = oldUnit->isEmbarked();
    const UnitType baseType = oldUnit->getEmbarkedBaseType();

    const PlayerId ownerId = oldUnit->getOwnerId();
    const Pos pos = oldUnit->getPos();

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

    *oldUnit = upgraded;

    // Upgraded unit may have different vision range – reveal accordingly.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}

bool UnitUpgradeSystem::upgradeRaftToBomber(Game& game, UnitId unitId) {
    if (!canUpgradeRaftToBomber(game, unitId)) {
        return false;
    }

    Unit* oldUnit = game.getUnit(unitId);
    if (!oldUnit) {
        return false;
    }

    Player& owner = game.getPlayer(oldUnit->getOwnerId());
    const int cost = std::max(0, UnitFactory::getUnitCost(UnitType::Bomber));
    if (!owner.spendStars(cost)) {
        return false;
    }

    // Preserve Raft HP and Max HP
    const int oldHp = oldUnit->getHealth();
    const int oldMaxHp = oldUnit->getMaxHealth();
    const bool poisoned = oldUnit->poisoned();
    const bool veteran = oldUnit->isVeteran();
    const int kills = oldUnit->getKillCounter();
    const bool wasEmbarked = oldUnit->isEmbarked();
    const UnitType baseType = oldUnit->getEmbarkedBaseType();

    const PlayerId ownerId = oldUnit->getOwnerId();
    const Pos pos = oldUnit->getPos();

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

    *oldUnit = upgraded;

    // Upgraded unit may have different vision range – reveal accordingly.
    VisionSystem::revealFromUnit(game, unitId);

    return true;
}
