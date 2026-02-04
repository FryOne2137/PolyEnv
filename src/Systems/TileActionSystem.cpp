//
// Created by Fryderyk Niedzwiecki on 25/01/2026.
//

#include "Systems/TileActionSystem.h"

#include "Game.h"
#include "World/Map.h"
#include "World/Tile.h"
#include "Player/Player.h"
#include "tech/TechDB.h"
#include "World/Settlements/City.h"

#include <algorithm>
#include <cstdint>

#include "MonumentSystem.h"

namespace {

// ---- common bitmask helpers ----
inline bool hasFlag(ResourcesEnum v, ResourcesEnum f) {
    return (static_cast<uint8_t>(v) & static_cast<uint8_t>(f)) != 0;
}
inline ResourcesEnum addFlag(ResourcesEnum v, ResourcesEnum f) {
    return static_cast<ResourcesEnum>(static_cast<uint8_t>(v) | static_cast<uint8_t>(f));
}
inline ResourcesEnum removeFlag(ResourcesEnum v, ResourcesEnum f) {
    return static_cast<ResourcesEnum>(static_cast<uint8_t>(v) & ~static_cast<uint8_t>(f));
}

// ---- common checks ----
inline bool requireTech(const Game& game, PlayerId pid, TechId tech) {
    // TechId::Count is used as "no requirement".
    if (tech == TechId::Count) return true;

    const auto idx = static_cast<uint32_t>(tech);
    const auto cnt = static_cast<uint32_t>(TechId::Count);

    // Defensive: if an invalid/unknown tech id is passed, do NOT allow the action.
    if (idx >= cnt) return false;

    return game.getPlayer(pid).hasTech(tech);
}

inline bool spendStars(Game& game, PlayerId pid, int cost) {
    cost = std::max(0, cost);
    if (cost == 0) return true;
    return game.getPlayer(pid).spendStars(cost);
}

// Enforce: action can be done only on current player's territory (city owned by pid)
inline bool ensureOwnTerritory(Game& game, PlayerId pid, Pos pos, CityId& outCid) {
    if (!game.getMap().inBounds(pos)) return false;
    const Tile& t = game.getMap().at(pos);

    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return false;

    const City* c = game.getCity(cid);
    if (!c) return false;

    if (static_cast<PlayerId>(c->getOwnerId()) != pid) return false;

    outCid = cid;
    return true;
}



} // namespace

// ------------------------------
//            ACTIONS
// ------------------------------

bool TileActionSystem::hunt(Game& game, PlayerId pid, Pos pos) {
    // Polytopia-style (adjust if you want):
    // Hunting tech, costs stars, Animals -> Crops, +1 pop.
    static constexpr TechId kTech = TechId::Hunting;
    static constexpr int kCost = 2;
    static constexpr int kPopGain = 1;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    const ResourcesEnum r = t.getResource();
    if (!hasFlag(r, ResourcesEnum::Animals)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    // Animals -> Crops
    ResourcesEnum nr = removeFlag(r, ResourcesEnum::Animals);
    nr = addFlag(nr, ResourcesEnum::Crops);
    t.setResource(nr);

    if (City* c = game.getCity(cid)) {
        (void)c->addPopulation(static_cast<uint8_t>(kPopGain));
    }
    return true;
}

bool TileActionSystem::oraganization(Game& game, PlayerId pid, Pos pos) {
    // Polytopia-style (adjust if you want):
    // Hunting tech, costs stars, Animals -> Crops, +1 pop.
    static constexpr TechId kTech = TechId::Organization;
    static constexpr int kCost = 2;
    static constexpr int kPopGain = 1;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    const ResourcesEnum r = t.getResource();
    if (!hasFlag(r, ResourcesEnum::Fruit)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    // Consume the fruit.
    ResourcesEnum nr = removeFlag(r, ResourcesEnum::Fruit);
    t.setResource(nr);

    if (City* c = game.getCity(cid)) {
        (void)c->addPopulation(static_cast<uint8_t>(kPopGain));
    }
    return true;
}



bool TileActionSystem::fishing(Game& game, PlayerId pid, Pos pos) {
    // Fishing tech:
    // - Fish: cost 2, remove Fish, +1 pop
    // - Whale: remove Whale, +10 stars (no cost)
    static constexpr TechId kTech = TechId::Fishing;
    static constexpr int kFishCost = 2;
    static constexpr int kFishPop = 1;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Water &&
        t.getBaseTerrain() != BaseTerrainEnum::Ocean) {
        return false;
    }

    ResourcesEnum r = t.getResource();

    if (hasFlag(r, ResourcesEnum::Fish)) {
        if (!spendStars(game, pid, kFishCost)) return false;
        t.setResource(removeFlag(r, ResourcesEnum::Fish));
        if (City* c = game.getCity(cid)) {
            (void)c->addPopulation(static_cast<uint8_t>(kFishPop));
        }
        return true;
    }


    return false;
}

bool TileActionSystem::clearForest(Game& game, PlayerId pid, Pos pos) {
    // Polytopia: Forestry tech, remove forest, +1 star, no cost.
    static constexpr TechId kTech = TechId::Forestry;
    static constexpr int kCost = 0;
    static constexpr int kReward = 1;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    const ResourcesEnum r = t.getResource();
    if (!hasFlag(r, ResourcesEnum::Forest)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    // Remove forest
    t.setResource(removeFlag(r, ResourcesEnum::Forest));

    // Reward stars
    game.getPlayer(pid).addStars(kReward);
    MonumentSystem::onStarsUpdated(game,pid);

    return true;
}


bool TileActionSystem::burnForest(Game& game, PlayerId pid, Pos pos) {
    // Placeholder: pick the tech you want for burn.
    // I suggest Spiritualism for “nature actions”.
    static constexpr TechId kTech = TechId::Construction;
    static constexpr int kCost = 5;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    (void)cid;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    const ResourcesEnum r = t.getResource();
    if (!hasFlag(r, ResourcesEnum::Forest)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    // Burn = remove forest resource (you can additionally set a “burned” resource later if you add one)
    t.setResource(removeFlag(r, ResourcesEnum::Forest));
    return true;
}

bool TileActionSystem::growForest(Game& game, PlayerId pid, Pos pos) {
    // Polytopia: Spiritualism unlocks Grow Forest.
    static constexpr TechId kTech = TechId::Spiritualism;
    static constexpr int kCost = 5;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    (void)cid;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    // Keep it strict: only on completely empty resource tile.
    if (t.getResource() != ResourcesEnum::None) return false;

    if (!spendStars(game, pid, kCost)) return false;

    t.setResource(addFlag(ResourcesEnum::None, ResourcesEnum::Forest));
    return true;
}

bool TileActionSystem::destroy(Game& game, PlayerId pid, Pos pos) {
    // Polytopia: Free Spirit unlocks Destroy.
    static constexpr TechId kTech = TechId::Chivalry;
    static constexpr int kCost = 0;

    CityId cid = kNoCity;
    if (!ensureOwnTerritory(game, pid, pos, cid)) return false;
    (void)cid;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);

    // Do not destroy the settlement itself.
    if (t.getSettlementType() == SettlementTypeEnum::City ||
        t.getSettlementType() == SettlementTypeEnum::Village) {
        return false;
    }

    const bool hasAnyRes = (t.getResource() != ResourcesEnum::None);
    const bool hasBuilding = (t.getBuildingType() != BuildingTypeEnum::None);
    const bool hasRoadBridge = (t.getRoadBridge() != RoadBridgeEnum::None);

    if (!hasAnyRes && !hasBuilding && !hasRoadBridge) return false;

    if (!spendStars(game, pid, kCost)) return false;

    if (hasAnyRes) t.setResource(ResourcesEnum::None);
    if (hasBuilding) t.setBuildingType(BuildingTypeEnum::None);
    if (hasRoadBridge) t.setRoadBridge(RoadBridgeEnum::None);

    return true;
}


bool TileActionSystem::buildRoad(Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Roads;
    static constexpr int kCost = 3;

    if (!game.getMap().inBounds(pos)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);

    // Only land tiles
    if (t.getBaseTerrain() != BaseTerrainEnum::Land) return false;

    // Must be own territory OR unclaimed territory
    const CityId cid = t.getTerritoryCityId();
    if (cid != kNoCity) {
        const City* c = game.getCity(cid);
        if (!c) return false;
        if (static_cast<PlayerId>(c->getOwnerId()) != pid) return false;
    }

    // Already has road/bridge
    if (t.getRoadBridge() != RoadBridgeEnum::None) return false;

    if (!spendStars(game, pid, kCost)) return false;

    t.setRoadBridge(RoadBridgeEnum::Road);
    return true;
}

bool TileActionSystem::buildBridge(Game& game, PlayerId pid, Pos pos) {
    static constexpr TechId kTech = TechId::Roads;
    static constexpr int kCost = 5;

    if (!game.getMap().inBounds(pos)) return false;
    if (!requireTech(game, pid, kTech)) return false;

    Tile& t = game.getMap().at(pos);

    // Bridge can be placed only on WATER (not Ocean).
    if (t.getBaseTerrain() != BaseTerrainEnum::Water) return false;

    // Must be own territory OR unclaimed territory
    const CityId cid = t.getTerritoryCityId();
    if (cid != kNoCity) {
        const City* c = game.getCity(cid);
        if (!c) return false;
        if (static_cast<PlayerId>(c->getOwnerId()) != pid) return false;
    }

    // Must not already have a road/bridge
    if (t.getRoadBridge() != RoadBridgeEnum::None) return false;

    auto isLandish = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;
        const BaseTerrainEnum bt = game.getMap().at(p).getBaseTerrain();
        return (bt == BaseTerrainEnum::Land) || (bt == BaseTerrainEnum::Mountain);
    };

    const bool ns = isLandish(Pos{pos.x, pos.y - 1}) && isLandish(Pos{pos.x, pos.y + 1});
    const bool ew = isLandish(Pos{pos.x - 1, pos.y}) && isLandish(Pos{pos.x + 1, pos.y});

    // Must connect across opposite sides (N-S or E-W), but NOT both (no cross).
    if (!(ns ^ ew)) return false;

    if (!spendStars(game, pid, kCost)) return false;

    t.setRoadBridge(RoadBridgeEnum::Bridge);
    return true;
}