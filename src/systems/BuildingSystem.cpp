#include "systems/BuildingSystem.h"

#include "../game/Game.h"
#include "CitiesConnectionSystem.h"
#include "systems/CitySystem.h"
#include "systems/PlayerSystem.h"
#include "world/Tile.h"
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

// --- Polytopia-style Forge/Sawmill/Windmill/Market helpers ---

static inline bool isFieldTile(const Tile& t) {
    return t.getBaseTerrain() == BaseTerrainEnum::Land;
}

static inline bool isEconBooster(BuildingTypeEnum b) {
    return b == BuildingTypeEnum::Forge || b == BuildingTypeEnum::Sawmill || b == BuildingTypeEnum::Windmill;
}

static inline bool sameOwnerCityTerritory(const Game& game, PlayerId pid, CityId cid) {
    if (cid == kNoCity) return false;
    if (!CitySystem::cityExists(game, cid)) return false;
    return static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) == pid;
}


static inline int countAdjacentBuildingsOwned(const Game& game, PlayerId pid, Pos center, BuildingTypeEnum bt) {
    const Map& map = game.getMap();
    int c = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Pos p{center.x + dx, center.y + dy};
            if (!map.inBounds(p)) continue;
            const Tile& t = map.at(p);
            if (t.getBuildingType() != bt) continue;
            if (!sameOwnerCityTerritory(game, pid, t.getTerritoryCityId())) continue;
            ++c;
        }
    }
    return c;
}

static inline int countAdjacentAnyBuildingsOwned(const Game& game, PlayerId pid, Pos center) {
    const Map& map = game.getMap();
    int c = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Pos p{center.x + dx, center.y + dy};
            if (!map.inBounds(p)) continue;
            const Tile& t = map.at(p);
            if (t.getBuildingType() == BuildingTypeEnum::None) continue;
            if (!sameOwnerCityTerritory(game, pid, t.getTerritoryCityId())) continue;
            ++c;
        }
    }
    return c;
}

static inline uint8_t clampBuildingLevel(int lvl) {
    // In Polytopia, these "adjacency buildings" are effectively capped (UI shows up to 8).
    return static_cast<uint8_t>(std::clamp(lvl, 1, 8));
}

static inline bool cityAlreadyHasBuilding(const Game& game, CityId cid, BuildingTypeEnum bt) {
    if (cid == kNoCity) return false;
    if (!CitySystem::cityExists(game, cid)) return false;
    const Map& map = game.getMap();
    const int W = map.getWidth();
    const int H = map.getHeight();
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Pos p{x, y};
            const Tile& t = map.at(p);
            if (t.getTerritoryCityId() != cid) continue;
            if (t.getBuildingType() == bt) return true;
        }
    }
    return false;
}

static inline int polytopiaPopGainForBuilding(const Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    // Polytopia rules:
    // - Sawmill: +1 pop per adjacent friendly Lumber Hut
    // - Windmill: +1 pop per adjacent friendly Farm
    // - Forge:    +2 pop per adjacent friendly Mine
    if (type == BuildingTypeEnum::Sawmill) {
        return countAdjacentBuildingsOwned(game, pid, pos, BuildingTypeEnum::LumberHut);
    }
    if (type == BuildingTypeEnum::Windmill) {
        return countAdjacentBuildingsOwned(game, pid, pos, BuildingTypeEnum::Farm);
    }
    if (type == BuildingTypeEnum::Forge) {
        return 2 * countAdjacentBuildingsOwned(game, pid, pos, BuildingTypeEnum::Mine);
    }
    return 0;
}

static inline bool marketHasRequiredAdjacentBoosterOwned(const Game& game, PlayerId pid, Pos pos) {
    const Map& map = game.getMap();
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Pos p{pos.x + dx, pos.y + dy};
            if (!map.inBounds(p)) continue;
            const Tile& t = map.at(p);
            if (!isEconBooster(t.getBuildingType())) continue;
            if (!sameOwnerCityTerritory(game, pid, t.getTerritoryCityId())) continue;
            return true;
        }
    }
    return false;
}

bool BuildingSystem::canBuild(const Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) return false;
    if (!BuildingDB::isDefined(type)) return false;

    const Tile& tile = map.at(pos);
    const auto& bi = BuildingDB::info(type);

    if (tile.getBuildingType() != BuildingTypeEnum::None) return false;

    // --- Polytopia special placement rules ---
    // Forge / Sawmill / Windmill / Market are Field-only in Polytopia.
    if (type == BuildingTypeEnum::Forge || type == BuildingTypeEnum::Sawmill || type == BuildingTypeEnum::Windmill || type == BuildingTypeEnum::Market) {
        if (!isFieldTile(tile)) return false;
    }

    // Only one Forge/Sawmill/Windmill per city.
    if (type == BuildingTypeEnum::Forge || type == BuildingTypeEnum::Sawmill || type == BuildingTypeEnum::Windmill) {
        const CityId cid = tile.getTerritoryCityId();
        if (cid == kNoCity) return false;
        if (!sameOwnerCityTerritory(game, pid, cid)) return false;
        if (cityAlreadyHasBuilding(game, cid, type)) return false;
    }

    // Market must be adjacent to at least one Forge/Sawmill/Windmill (owned).
    if (type == BuildingTypeEnum::Market) {
        const CityId cid = tile.getTerritoryCityId();
        if (cid == kNoCity) return false;
        if (!sameOwnerCityTerritory(game, pid, cid)) return false;
        if (!marketHasRequiredAdjacentBoosterOwned(game, pid, pos)) return false;
    }

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

    // Polytopia resource consumption rules when building on a tile.

    // Buildings consume Fruit.
    if (tile.hasResourceFlag(ResourcesEnum::Fruit)) {
        tile.removeResourceFlag(ResourcesEnum::Fruit);
    }

    // Port consumes Fish on the tile.
    if (type == BuildingTypeEnum::Port && tile.hasResourceFlag(ResourcesEnum::Fish)) {
        tile.removeResourceFlag(ResourcesEnum::Fish);
    }

    // Sawmill consumes Animal on the tile.
    if (type == BuildingTypeEnum::Sawmill && tile.hasResourceFlag(ResourcesEnum::Animal)) {
        tile.removeResourceFlag(ResourcesEnum::Animal);
    }

    // Ports affect city connections.
    if (type == BuildingTypeEnum::Port) {
        CitiesConnectionSystem::update(game);
    }

    if (isMonument(type)) {
        (void)PlayerSystem::placeMonument(game, pid, type);
    }

    const CityId cid = tile.getTerritoryCityId();
    if (cid != kNoCity && CitySystem::cityExists(game, cid)) {
        // Base population gain from DB.
        const auto& bi = BuildingDB::info(type);
        int addPop = static_cast<int>(bi.populationGain);

        // Polytopia: Forge/Sawmill/Windmill provide population dynamically based on adjacent friendly buildings.
        if (type == BuildingTypeEnum::Forge || type == BuildingTypeEnum::Sawmill || type == BuildingTypeEnum::Windmill) {
            addPop += polytopiaPopGainForBuilding(game, pid, pos, type);
        }

        if (addPop != 0) {
            (void)CitySystem::addPopulation(game, cid, static_cast<uint16_t>(std::max(0, addPop)));
        }

        // If we built Mine/Farm/LumberHut, it may increase the level (population) of adjacent Forge/Windmill/Sawmill.
        if (type == BuildingTypeEnum::Mine || type == BuildingTypeEnum::Farm || type == BuildingTypeEnum::LumberHut) {
            const Map& map2 = game.getMap();
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const Pos ap{pos.x + dx, pos.y + dy};
                    if (!map2.inBounds(ap)) continue;
                    const Tile& at = map2.at(ap);
                    const CityId ac = at.getTerritoryCityId();
                    if (!sameOwnerCityTerritory(game, pid, ac)) continue;

                    const BuildingTypeEnum ab = at.getBuildingType();
                    int delta = 0;
                    if (type == BuildingTypeEnum::Mine && ab == BuildingTypeEnum::Forge) delta = 2;
                    if (type == BuildingTypeEnum::Farm && ab == BuildingTypeEnum::Windmill) delta = 1;
                    if (type == BuildingTypeEnum::LumberHut && ab == BuildingTypeEnum::Sawmill) delta = 1;

                    if (delta > 0) {
                        (void)CitySystem::addPopulation(game, ac, static_cast<uint16_t>(delta));
                    }
                }
            }
        }
    }

    return true;
}

bool BuildingSystem::buildMonument(Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    // Kept for explicit monument calls; logic lives in build()/canBuild().
    return build(game, pid, pos, type);
}


uint8_t BuildingSystem::getBuildingLevel(const Game& game, PlayerId pid, Pos pos) {
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) return 0;
    const Tile& t = map.at(pos);
    const BuildingTypeEnum bt = t.getBuildingType();
    if (bt == BuildingTypeEnum::None) return 0;
    return getBuildingLevelForTypeAt(game, pid, pos, bt);
}

uint8_t BuildingSystem::getBuildingLevelForTypeAt(const Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type) {
    // Default: level 1.
    // Lumber Hut + Farm are always level 1 (as requested).
    switch (type) {
        case BuildingTypeEnum::LumberHut:
        case BuildingTypeEnum::Farm:
            return 1;

        // Adjacency-scaled production buildings
        case BuildingTypeEnum::Sawmill: {
            // Level depends on adjacent friendly Lumber Huts.
            const int adj = countAdjacentBuildingsOwned(game, pid, pos, BuildingTypeEnum::LumberHut);
            return clampBuildingLevel(std::max(1, adj));
        }
        case BuildingTypeEnum::Windmill: {
            // Level depends on adjacent friendly Farms.
            const int adj = countAdjacentBuildingsOwned(game, pid, pos, BuildingTypeEnum::Farm);
            return clampBuildingLevel(std::max(1, adj));
        }
        case BuildingTypeEnum::Forge: {
            // Level depends on adjacent friendly Mines.
            const int adj = countAdjacentBuildingsOwned(game, pid, pos, BuildingTypeEnum::Mine);
            return clampBuildingLevel(std::max(1, adj));
        }
        case BuildingTypeEnum::Market: {
            // Polytopia: Market level is the SUM of levels of adjacent (owned) Sawmill/Windmill/Forge.
            // See: https://polytopia.fandom.com/wiki/Market
            const Map& map = game.getMap();
            int sum = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const Pos p{pos.x + dx, pos.y + dy};
                    if (!map.inBounds(p)) continue;
                    const Tile& t2 = map.at(p);

                    const BuildingTypeEnum bt2 = t2.getBuildingType();
                    if (bt2 != BuildingTypeEnum::Sawmill && bt2 != BuildingTypeEnum::Windmill && bt2 != BuildingTypeEnum::Forge) continue;
                    if (!sameOwnerCityTerritory(game, pid, t2.getTerritoryCityId())) continue;

                    // Add the *current* level of the adjacent booster.
                    sum += static_cast<int>(getBuildingLevelForTypeAt(game, pid, p, bt2));
                }
            }

            // Market placement requires at least one adjacent booster, so sum should be >= 1.
            return clampBuildingLevel(std::max(1, sum));
        }

        default:
            return 1;
    }
}