//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//


#include "UnitSpawnSystem.h"
#include "systems/CitySystem.h"
#include "systems/UnitSystem.h"
#include "systems/PlayerSystem.h"


#include <algorithm>
#include <iostream>

UnitId UnitSpawnSystem::spawnUnit(Game& game, Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately) {
    // --- Basic placement checks ---
    if (!map.inBounds(pos)) return kNoUnit;
    if (map.unitOn(pos) != Map::kNoUnit) return kNoUnit;

    // Units can be spawned ONLY on city tiles.
    const Tile& t = map.at(pos);
    if (t.getSettlementType() != SettlementTypeEnum::City) {
        return kNoUnit;
    }

    // The city tile defines which city the unit belongs to.
    const CityId cityForUnit = static_cast<CityId>(t.getSettlementId());

    // City must exist.
    if (!CitySystem::cityExists(game, cityForUnit)) {
        return kNoUnit;
    }

    // The city must belong to the spawning player.
    if (static_cast<PlayerId>(CitySystem::getCityOwner(game, cityForUnit)) != owner) {
        return kNoUnit;
    }

    // Capacity check (prevents training/spawning when the city is full).
    if (CitySystem::getCityUnitsCount(game, cityForUnit) >= CitySystem::getCityMaxUnitCapacity(game, cityForUnit)) {
        return kNoUnit;
    }

    // --- Create prototype with factory (stats + required tech) ---
    UnitId id = static_cast<UnitId>(game.units.size());

    Unit u = UnitFactory::create(type, owner, pos);
    u.setId(id);

    // Tech requirement (tagged by UnitFactory)
    {
        const TechId req = u.getRequiredTechToSpawn();
        if (req != TechId::Count) {
            if (!PlayerSystem::hasTech(game, owner, req)) {
                return kNoUnit;
            }
        }
    }

    // Spend stars for trained units (start-of-game spawns are free).
    if (!canActImmediately) {
        const int cost = std::max(0, u.getCost());
        if (!PlayerSystem::spendStars(game, owner, cost)) {
            return kNoUnit;
        }
    }

    // --- Commit to game state ---
    game.units.push_back(u);
    map.setUnitOn(pos, id);
    PlayerSystem::addUnit(game, owner, id);

    // Freshly spawned unit turn flags (via UnitSystem).
    (void)UnitSystem::setMovedThisTurn(game, id, !canActImmediately);
    (void)UnitSystem::setAttackedThisTurn(game, id, !canActImmediately);
    (void)UnitSystem::setVeteran(game, id, false);
    (void)UnitSystem::setPoisoned(game, id, false);
    (void)UnitSystem::setKillCounter(game, id, 0);

    // Register the unit in its city (for capacity / city bookkeeping).
    CitySystem::addUnitToCity(game, id, cityForUnit);

    VisionSystem::revealFromUnit(game, id);
    return id;
}

UnitId UnitSpawnSystem::spawnUnitForced(Game& game, Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately, bool makeVeteran) {

    if (!map.inBounds(pos)) {
        return kNoUnit;
    }
    const UnitId occ0 = map.unitOn(pos);
    if (occ0 != Map::kNoUnit) {
        return kNoUnit;
    }

    // --- Create prototype with factory (stats + required tech) ---
    UnitId id = static_cast<UnitId>(game.units.size());
    Unit u = UnitFactory::create(type, owner, pos);
    u.setId(id);

    // Forced spawns are free; canActImmediately controls readiness.

    // --- Commit to game state ---
    game.units.push_back(u);
    map.setUnitOn(pos, id);
    PlayerSystem::addUnit(game, owner, id);

    // Forced spawns are free; canActImmediately controls readiness (via UnitSystem).
    (void)UnitSystem::setMovedThisTurn(game, id, !canActImmediately);
    (void)UnitSystem::setAttackedThisTurn(game, id, !canActImmediately);
    (void)UnitSystem::setVeteran(game, id, makeVeteran);
    (void)UnitSystem::setPoisoned(game, id, false);
    (void)UnitSystem::setKillCounter(game, id, 0);

    VisionSystem::revealFromUnit(game, id);
    return id;
}