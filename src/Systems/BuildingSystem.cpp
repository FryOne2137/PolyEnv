#include "Systems/BuildingSystem.h"

#include "Game.h"
#include "World/Tile.h"
#include "Player/Player.h"
#include "Buildings/BuildingDB.h"

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

    if (bi.requiresTerritory && tile.getTerritoryCityId() == kNoCity) return false;

    if (!terrainAllowed(bi.allowedTerrainMask, tile.getBaseTerrain())) return false;

    if (bi.requiredResourceMask != 0) {
        if (!resourceAllowed(bi.requiredResourceMask, tile.getResource())) return false;
    }

    if (bi.requiredTech != TechId::Count) {
        if (!game.getPlayer(pid).hasTech(bi.requiredTech)) return false;
    }

    const int cost = BuildingDB::getCost(type);
    if (cost > 0 && game.getPlayer(pid).getStars() < cost) return false;

    return true;
}

bool BuildingSystem::build(Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    if (!canBuild(game, pid, pos, type)) return false;

    Tile& tile = game.getMap().at(pos);
    Player& p = game.getPlayer(pid);

    const int cost = BuildingDB::getCost(type);
    if (!p.spendStars(cost)) return false;

    tile.setBuildingType(type);

    const CityId cid = tile.getTerritoryCityId();
    if (cid != kNoCity) {
        if (City* c = game.getCity(cid)) {
            const auto& bi = BuildingDB::info(type);
            const uint16_t curPop = c->getPopulation();
            const uint16_t addPop = static_cast<uint16_t>(bi.populationGain);
            c->setPopulation(static_cast<uint16_t>(curPop + addPop));

            // If the building adds multiple population, the city might level up multiple times.
            while (c->canLevelUp()) {
                if (!c->levelUp()) break;
            }
        }
    }

    return true;
}