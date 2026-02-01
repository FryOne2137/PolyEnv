//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//

#include "UnitSpawnSystem.h"


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
    City* c = game.getCity(cityForUnit);
    if (!c) return kNoUnit;

    // The city must belong to the spawning player.
    if (static_cast<PlayerId>(c->getOwnerId()) != owner) {
        return kNoUnit;
    }

    // Capacity check (prevents training/spawning when the city is full).
    if (c->getUnitsCount() >= c->maxUnitCapacity()) {
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
            if (!game.getPlayer(owner).hasTech(req)) {
                return kNoUnit;
            }
        }
    }

    // Spend stars for trained units (start-of-game spawns are free).
    if (!canActImmediately) {
        const int cost = std::max(0, u.getCost());
        if (!game.getPlayer(owner).spendStars(cost)) {
            return kNoUnit;
        }
    }

    // Freshly spawned unit turn flags.
    u.setMovedThisTurn(!canActImmediately);
    u.setAttackedThisTurn(!canActImmediately);
    u.setVeteran(false);
    u.setPoisoned(false);
    u.setKillCounter(0);

    // --- Commit to game state ---
    game.units.push_back(u);
    map.setUnitOn(pos, id);
    game.getPlayer(owner).addUnit(id);

    // Register the unit in its city (for capacity / city bookkeeping).
    c->addUnit(id);

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
    u.setMovedThisTurn(!canActImmediately);
    u.setAttackedThisTurn(!canActImmediately);
    u.setVeteran(makeVeteran);
    u.setPoisoned(false);
    u.setKillCounter(0);


    // --- Commit to game state ---
    game.units.push_back(u);
    map.setUnitOn(pos, id);
    game.getPlayer(owner).addUnit(id);

    VisionSystem::revealFromUnit(game, id);
    return id;
}