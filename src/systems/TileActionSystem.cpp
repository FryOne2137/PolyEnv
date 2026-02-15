//
// Created by Fryderyk Niedzwiecki on 25/01/2026.
//

#include "systems/TileActionSystem.h"

#include "../game/Game.h"
#include "systems/PlayerSystem.h"
#include "world/Map.h"
#include "world/Tile.h"
#include "../content/tech/TechDB.h"
#include <algorithm>
#include <cstdint>

#include "CitySystem.h"
#include "MonumentSystem.h"
#include "CitiesConnectionSystem.h"

namespace {

// ---- resource helpers (single resource enum) ----
inline bool hasFlag(ResourcesEnum v, ResourcesEnum f) {
    return v == f;
}
inline ResourcesEnum addFlag(ResourcesEnum v, ResourcesEnum f) {
    (void)v;
    return f;
}
inline ResourcesEnum removeFlag(ResourcesEnum v, ResourcesEnum f) {
    return (v == f) ? ResourcesEnum::None : v;
}

// ---- common checks ----
inline bool requireTech(const Game& game, PlayerId pid, TechId tech) {
    if (tech == TechId::Count) return true;

    const auto idx = static_cast<uint32_t>(tech);
    const auto cnt = static_cast<uint32_t>(TechId::Count);

    if (idx >= cnt) return false;
    return PlayerSystem::hasTech(game, pid, tech);
}

inline bool canSpendStars(const Game& game, PlayerId pid, int cost) {
    cost = std::max(0, cost);
    return PlayerSystem::getStars(game, pid) >= cost;
}

inline bool spendStars(Game& game, PlayerId pid, int cost) {
    cost = std::max(0, cost);
    if (cost == 0) return true;
    return PlayerSystem::spendStars(game, pid, cost);
}

inline bool ensureOwnTerritory(const Game& game, PlayerId pid, Pos pos, CityId& outCid) {
    if (!game.getMap().inBounds(pos)) return false;
    const Tile& t = game.getMap().at(pos);

    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return false;

    if (!CitySystem::cityExists(game, cid)) return false;
    if (static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) != pid) return false;

    outCid = cid;
    return true;
}

} // namespace

bool TileActionSystem::canHunt(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Hunting;
    static constexpr int kCost = 2;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land && t.getBaseTerrain() != BaseTerrainEnum::Forest) return false;
    return hasFlag(t.getResource(), ResourcesEnum::Animal);
}

bool TileActionSystem::hunt(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 2;
    static constexpr int kPopGain = 1;

    if (!canHunt(game, pid, pos)) return false;

    CityId cid = kNoCity;
    (void)ensureOwnTerritory(game, pid, pos, cid);

    Tile& t = game.getMap().at(pos);
    const ResourcesEnum r = t.getResource();

    if (!spendStars(game, pid, kCost)) return false;

    t.setResource(removeFlag(r, ResourcesEnum::Animal));
    (void)CitySystem::addPopulation(game, cid, kPopGain);

    return true;
}

bool TileActionSystem::canOrganization(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Organization;
    static constexpr int kCost = 2;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;
    return hasFlag(t.getResource(), ResourcesEnum::Fruit);
}

bool TileActionSystem::organization(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 2;
    static constexpr int kPopGain = 1;

    if (!canOrganization(game, pid, pos)) return false;

    CityId cid = kNoCity;
    (void)ensureOwnTerritory(game, pid, pos, cid);

    Tile& t = game.getMap().at(pos);
    const ResourcesEnum r = t.getResource();

    if (!spendStars(game, pid, kCost)) return false;

    t.setResource(removeFlag(r, ResourcesEnum::Fruit));
    (void)CitySystem::addPopulation(game, cid, static_cast<uint16_t>(kPopGain));
    return true;
}

bool TileActionSystem::canFishing(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Fishing;
    static constexpr int kFishCost = 2;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Water &&
        t.getBaseTerrain() != BaseTerrainEnum::Ocean) {
        return false;
    }

    if (hasFlag(t.getResource(), ResourcesEnum::Fish)) {
        return canSpendStars(game, pid, kFishCost);
    }

    return false;
}

bool TileActionSystem::fishing(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kFishCost = 2;
    static constexpr int kFishPop = 1;

    if (!canFishing(game, pid, pos)) return false;

    CityId cid = kNoCity;
    (void)ensureOwnTerritory(game, pid, pos, cid);

    Tile& t = game.getMap().at(pos);
    ResourcesEnum r = t.getResource();

    if (hasFlag(r, ResourcesEnum::Fish)) {
        if (!spendStars(game, pid, kFishCost)) return false;
        t.setResource(removeFlag(r, ResourcesEnum::Fish));
        (void)CitySystem::addPopulation(game, cid, static_cast<uint16_t>(kFishPop));
        return true;
    }

    return false;
}

bool TileActionSystem::canClearForest(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Forestry;
    static constexpr int kCost = 0;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Forest) return false;
    if (t.getBuildingType() != BuildingTypeEnum::None) return false;

    // Only "animal-capable" forests are valid targets: empty forest or forest with Animal.
    const ResourcesEnum r = t.getResource();
    return r == ResourcesEnum::None || hasFlag(r, ResourcesEnum::Animal);
}

bool TileActionSystem::clearForest(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 0;
    static constexpr int kReward = 1;

    if (!canClearForest(game, pid, pos)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    Tile& t = game.getMap().at(pos);
    const ResourcesEnum r = t.getResource();
    t.setBaseTerrain(BaseTerrainEnum::Land);
    t.setResource(removeFlag(r, ResourcesEnum::Animal));
    PlayerSystem::addStars(game, pid, kReward);

    return true;
}

bool TileActionSystem::canBurnForest(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Construction;
    static constexpr int kCost = 5;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Forest) return false;
    if (t.getBuildingType() != BuildingTypeEnum::None) return false;

    // Only "animal-capable" forests are valid targets: empty forest or forest with Animal.
    const ResourcesEnum r = t.getResource();
    return r == ResourcesEnum::None || hasFlag(r, ResourcesEnum::Animal);
}

bool TileActionSystem::burnForest(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 5;

    if (!canBurnForest(game, pid, pos)) return false;

    Tile& t = game.getMap().at(pos);
    const ResourcesEnum r = t.getResource();

    if (!spendStars(game, pid, kCost)) return false;

    t.setBaseTerrain(BaseTerrainEnum::Land);
    t.setResource(addFlag(removeFlag(r, ResourcesEnum::Animal), ResourcesEnum::Crops));
    return true;
}

bool TileActionSystem::canGrowForest(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Spiritualism;
    static constexpr int kCost = 5;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;
    return t.getResource() == ResourcesEnum::None;
}

bool TileActionSystem::growForest(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 5;

    if (!canGrowForest(game, pid, pos)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    Tile& t = game.getMap().at(pos);
    t.setBaseTerrain(BaseTerrainEnum::Forest);
    return true;
}

bool TileActionSystem::canDestroy(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Chivalry;
    static constexpr int kCost = 0;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);

    if (t.getSettlementType() == SettlementTypeEnum::City ||
        t.getSettlementType() == SettlementTypeEnum::Village) {
        return false;
    }

    const bool hasAnyRes = (t.getResource() != ResourcesEnum::None);
    const bool hasBuilding = (t.getBuildingType() != BuildingTypeEnum::None);
    const bool hasBridge = (t.getRoadBridge() == RoadBridgeEnum::Bridge);

    // Roads are intentionally not destroyable; bridges are destroyable.
    return hasAnyRes || hasBuilding || hasBridge;
}

bool TileActionSystem::destroy(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 0;

    if (!canDestroy(game, pid, pos)) return false;
    if (!spendStars(game, pid, kCost)) return false;

    Tile& t = game.getMap().at(pos);

    const bool hasAnyRes = (t.getResource() != ResourcesEnum::None);
    const bool hasBuilding = (t.getBuildingType() != BuildingTypeEnum::None);
    const bool hasBridge = (t.getRoadBridge() == RoadBridgeEnum::Bridge);

    if (hasAnyRes) t.setResource(ResourcesEnum::None);
    if (hasBuilding) t.setBuildingType(BuildingTypeEnum::None);
    if (hasBridge) t.setRoadBridge(RoadBridgeEnum::None);

    CitiesConnectionSystem::update(game);
    return true;
}

bool TileActionSystem::canBuildRoad(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Roads;
    static constexpr int kCost = 3;

    if (!game.getMap().inBounds(pos)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    const CityId cid = t.getTerritoryCityId();
    if (cid != kNoCity) {
        if (!CitySystem::cityExists(game, cid)) return false;
        if (static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) != pid) return false;
    }

    return t.getRoadBridge() == RoadBridgeEnum::None;
}

bool TileActionSystem::buildRoad(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 3;

    if (!canBuildRoad(game, pid, pos)) return false;
    if (!spendStars(game, pid, kCost)) return false;

    Tile& t = game.getMap().at(pos);
    t.setRoadBridge(RoadBridgeEnum::Road);

    CitiesConnectionSystem::update(game);
    return true;
}

bool TileActionSystem::canBuildBridge(const Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Roads;
    static constexpr int kCost = 5;

    if (!game.getMap().inBounds(pos)) return false;
    if (!requireTech(game, pid, kTech)) return false;
    if (!canSpendStars(game, pid, kCost)) return false;

    const Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Water) return false;

    const CityId cid = t.getTerritoryCityId();
    if (cid != kNoCity) {
        if (!CitySystem::cityExists(game, cid)) return false;
        if (static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) != pid) return false;
    }

    if (t.getRoadBridge() != RoadBridgeEnum::None) return false;

    auto isLandish = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;
        const BaseTerrainEnum bt = game.getMap().at(p).getBaseTerrain();
        return (bt == BaseTerrainEnum::Land) || (bt == BaseTerrainEnum::Mountain);
    };

    const bool ns = isLandish(Pos{pos.x, pos.y - 1}) && isLandish(Pos{pos.x, pos.y + 1});
    const bool ew = isLandish(Pos{pos.x - 1, pos.y}) && isLandish(Pos{pos.x + 1, pos.y});

    return (ns ^ ew);
}

bool TileActionSystem::buildBridge(Game& game, PlayerId pid, Pos pos) {
    static constexpr int kCost = 5;

    if (!canBuildBridge(game, pid, pos)) return false;
    if (!spendStars(game, pid, kCost)) return false;

    Tile& t = game.getMap().at(pos);
    t.setRoadBridge(RoadBridgeEnum::Bridge);

    CitiesConnectionSystem::update(game);
    return true;
}
