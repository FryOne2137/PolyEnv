#include "Systems/BuildingSystem.h"

#include "Game.h"
#include "CitiesConnectionSystem.h"
#include "Systems/CitySystem.h"
#include "Systems/PlayerSystem.h"
#include "World/Tile.h"
#include "Buildings/BuildingDB.h"
#include "terrain/BuildingTypeEnum.h"

static inline bool isMonument(BuildingTypeEnum t) {
    switch (t) {
        case BuildingTypeEnum::AltarOfPeace:
        case BuildingTypeEnum::EmperorsTomb:
        case BuildingTypeEnum::EyeOfGod:
        case BuildingTypeEnum::GateOfPower:
        case BuildingTypeEnum::GrandBazaar:
        case BuildingTypeEnum::ParkOfFortune:
        case BuildingTypeEnum::TowerOfWisdom:
            return true;
        default:
            return false;
    }
}


static inline bool terrainAllowed(BuildingDB::TerrainMask mask, BaseTerrainEnum t) {
    const uint8_t bit = uint8_t(1u) << static_cast<uint8_t>(t);
    return (mask & bit) != 0;
}

static inline bool resourceAllowed(BuildingDB::ResourceMask mask, ResourcesEnum r) {
    const uint8_t rb = static_cast<uint8_t>(r); // r to już bit
    return (mask & rb) != 0;
}

bool BuildingSystem::canBuild(const Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) return false;
    if (!BuildingDB::isDefined(type)) return false;

    const Tile& tile = map.at(pos);
    const auto& bi = BuildingDB::info(type);

    if (tile.getBuildingType() != BuildingTypeEnum::None) return false;

    // Monument rules: must be earned first and can only be placed once.
    if (isMonument(type)) {
        if (!PlayerSystem::hasEarnedMonument(game, pid, type)) return false;
        if (PlayerSystem::hasPlacedMonument(game, pid, type)) return false;
    }

    if (bi.requiresTerritory && tile.getTerritoryCityId() == kNoCity) return false;

    if (!terrainAllowed(bi.allowedTerrainMask, tile.getBaseTerrain())) return false;

    if (bi.requiredResourceMask != 0) {
        if (!resourceAllowed(bi.requiredResourceMask, tile.getResource())) return false;
    }

    if (bi.requiredTech != TechId::Count) {
        if (!PlayerSystem::hasTech(game, pid, bi.requiredTech)) return false;
    }

    const int cost = BuildingDB::getCost(type);
    if (cost > 0 && PlayerSystem::getStars(game, pid) < cost) return false;

    return true;
}

bool BuildingSystem::build(Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    if (!canBuild(game, pid, pos, type)) return false;

    Tile& tile = game.getMap().at(pos);

    const int cost = BuildingDB::getCost(type);
    if (!PlayerSystem::spendStars(game, pid, cost)) return false;

    tile.setBuildingType(type);

    // Ports affect city connections.
    if (type == BuildingTypeEnum::Port) {
        CitiesConnectionSystem::update(game);
    }

    if (isMonument(type)) {
        (void)PlayerSystem::placeMonument(game, pid, type);
    }

    const CityId cid = tile.getTerritoryCityId();
    if (cid != kNoCity && CitySystem::cityExists(game, cid)) {
        const auto& bi = BuildingDB::info(type);
        const uint16_t addPop = static_cast<uint16_t>(bi.populationGain);
        (void)CitySystem::addPopulation(game, cid, addPop);
    }

    return true;
}

bool BuildingSystem::buildMonument(Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    // Kept for explicit monument calls; logic lives in build()/canBuild().
    return build(game, pid, pos, type);
}