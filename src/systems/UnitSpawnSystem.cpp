//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//


#include "UnitSpawnSystem.h"
#include "systems/CitySystem.h"
#include "systems/UnitSystem.h"
#include "systems/PlayerSystem.h"


#include <algorithm>
#include <iostream>

namespace {
bool isCityTrainableUnit(UnitType type) {
    switch (type) {
        case UnitType::Warrior:
        case UnitType::Archer:
        case UnitType::Defender:
        case UnitType::Rider:
        case UnitType::MindBender:
        case UnitType::Swordsman:
        case UnitType::Catapult:
        case UnitType::Cloak:
        case UnitType::Knight:
            return true;
        default:
            return false;
    }
}
}

bool UnitSpawnSystem::canSpawnUnit(const Game& game, const Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately) {
    if (owner == kNoPlayer) return false;
    if (type == UnitType::Unknown) return false;
    if (!PlayerSystem::playerExists(game, owner)) return false;

    // City training rules:
    // - Giants are level-up rewards, not trainable.
    // - Dagger/Pirate/Bunny/Bunta are not city-trained units.
    // - Naval conversion/upgrade units (Raft/Scout/Rammer/Bomber/Juggernaut/Dinghy/Pirate) are not trained in city.
    // - Special-tribe units are not city-trained in this ruleset.
    if (!canActImmediately && !isCityTrainableUnit(type)) {
        return false;
    }

    // --- Basic placement checks ---
    if (!map.inBounds(pos)) return false;
    if (map.unitOn(pos) != Map::kNoUnit) return false;

    // Units can be spawned ONLY on city tiles.
    const Tile& t = map.at(pos);
    if (t.getSettlementType() != SettlementTypeEnum::City) {
        return false;
    }

    // The city tile defines which city the unit belongs to.
    const CityId cityForUnit = static_cast<CityId>(t.getSettlementId());

    // City must exist.
    if (!CitySystem::cityExists(game, cityForUnit)) {
        return false;
    }

    // The city must belong to the spawning player.
    if (static_cast<PlayerId>(CitySystem::getCityOwner(game, cityForUnit)) != owner) {
        return false;
    }

    // Capacity check (prevents training/spawning when the city is full).
    if (CitySystem::getCityUnitsCount(game, cityForUnit) >= CitySystem::getCityMaxUnitCapacity(game, cityForUnit)) {
        return false;
    }

    // Tech requirement (tagged by UnitFactory / unit templates).
    try {
        Unit probe = UnitFactory::create(type, owner, pos);
        const TechId req = probe.getRequiredTechToSpawn();
        if (req != TechId::Count && !PlayerSystem::hasTech(game, owner, req)) {
            return false;
        }

        // Spendability for trained units (start-of-game spawns are free).
        if (!canActImmediately) {
            const int cost = std::max(0, probe.getCost());
            if (PlayerSystem::getStars(game, owner) < cost) {
                return false;
            }
        }
    } catch (...) {
        // Unknown/missing template -> not spawnable.
        return false;
    }

    return true;
}

UnitId UnitSpawnSystem::spawnUnit(Game& game, Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately) {
    if (!canSpawnUnit(game, map, type, owner, pos, canActImmediately)) return kNoUnit;

    // The city tile defines which city the unit belongs to.
    const Tile& t = map.at(pos);
    const CityId cityForUnit = static_cast<CityId>(t.getSettlementId());

    // --- Create prototype with factory (stats + required tech) ---
    UnitId id = static_cast<UnitId>(game.units.size());

    Unit u = UnitFactory::create(type, owner, pos);
    u.setId(id);

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
