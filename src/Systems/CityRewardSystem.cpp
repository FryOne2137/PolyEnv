//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#include "CityRewardSystem.h"

#include "CitySystem.h"
#include "PlayerSystem.h"

// Jeśli masz osobne systemy, podepnij tu:
// #include "UnitSystem.h"
// #include "TerritorySystem.h"

#include <cstdint>


#include "MovementSystem.h"
#include "UnitFactory.h"
#include "UnitSpawnSystem.h"
#include "VisionSystem.h"
#include "World/Map.h"

namespace {

// Wiki: awans na poziom n wymaga n populacji (od poprzedniego poziomu).
// To ma sens, jeśli w City trzymasz "progress/population toward next level" (nie total).
static inline uint8_t requiredPopForNextLevel(uint8_t nextLevel) {
    return nextLevel; // lvl2 -> 2, lvl3 -> 3, lvl4 -> 4, ...
}

static inline bool isValidChoiceForLevel(CityUpgradeChoice c, uint8_t newLevel) {
    if (newLevel == 2) return c == CityUpgradeChoice::Workshop || c == CityUpgradeChoice::Explorer;
    if (newLevel == 3) return c == CityUpgradeChoice::CityWall || c == CityUpgradeChoice::Resources;
    if (newLevel == 4) return c == CityUpgradeChoice::PopulationGrowth || c == CityUpgradeChoice::BorderGrowth;
    if (newLevel >= 5) return c == CityUpgradeChoice::Park || c == CityUpgradeChoice::SuperUnit;
    return false;
}

} // namespace

bool CityRewardSystem::canUpgrade(const Game& game, CityId cityId) {
    if (!CitySystem::cityExists(game, cityId)) return false;

    const uint8_t level = CitySystem::getCityLevel(game, cityId);
    const uint8_t nextLevel = static_cast<uint8_t>(level + 1);

    const int16_t pop = CitySystem::getCityPopulation(game, cityId);
    if (pop < 0) return false;

    const uint8_t need = requiredPopForNextLevel(nextLevel);
    return pop >= need;
}

uint8_t CityRewardSystem::getNextLevel(const Game& game, CityId cityId) {
    if (!CitySystem::cityExists(game, cityId)) return 0;
    return static_cast<uint8_t>(CitySystem::getCityLevel(game, cityId) + 1);
}

CityUpgradeOptions CityRewardSystem::getUpgradeOptions(const Game& game, CityId cityId) {
    CityUpgradeOptions out{};

    if (!CitySystem::cityExists(game, cityId)) return out;

    const uint8_t newLevel = static_cast<uint8_t>(CitySystem::getCityLevel(game, cityId));

    // Zwracaj zawsze "dwie opcje" dla danego levelu.
    if (newLevel == 2) { out.a = CityUpgradeChoice::Workshop; out.b = CityUpgradeChoice::Explorer; return out; }
    if (newLevel == 3) { out.a = CityUpgradeChoice::CityWall; out.b = CityUpgradeChoice::Resources; return out; }
    if (newLevel == 4) { out.a = CityUpgradeChoice::PopulationGrowth; out.b = CityUpgradeChoice::BorderGrowth; return out; }
    if (newLevel >= 5) { out.a = CityUpgradeChoice::Park; out.b = CityUpgradeChoice::SuperUnit; return out; }

    return out;
}

bool CityRewardSystem::tryUpgrade(Game& game, CityId cityId, CityUpgradeChoice choice) {
    if (!CitySystem::cityExists(game, cityId)) return false;

    const uint8_t level = CitySystem::getCityLevel(game, cityId);
    const PlayerId pid = CitySystem::getCityOwner(game, cityId);
    const Pos pos=CitySystem::getCityPos(game, cityId);

    if (!isValidChoiceForLevel(choice, level)) return false;

    // Apply reward for the already-performed level-up.
    const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId));

    switch (choice) {
        case CityUpgradeChoice::None:
            return false;
        case CityUpgradeChoice::Workshop: {
            // Workshop: +1 star/turn w tym mieście.
            CitySystem::setCityWorkshopEnabled(game, cityId, true);
            // Jeśli workshop nie jest liczony automatycznie gdzie indziej:
        } break;

        case CityUpgradeChoice::Explorer: {
            VisionSystem::doExplorer(game, pid, pos);
        } break;

        case CityUpgradeChoice::CityWall: {
            CitySystem::setCityWallEnabled(game, cityId, true);
        } break;

        case CityUpgradeChoice::Resources: {
            // Resources: natychmiast +5 stars dla gracza.
            PlayerSystem::addStars(game, owner, 5);
        } break;

        case CityUpgradeChoice::PopulationGrowth: {
            // +3 population natychmiast.
            CitySystem::addPopulation(game, cityId, 3);
        } break;

        case CityUpgradeChoice::BorderGrowth: {
            const SettlementId sid = static_cast<SettlementId>(cityId);

            CitySystem::claimFreeTerritoryRadius(game,cityId,sid,pos,2);
        } break;

        case CityUpgradeChoice::Park: {
            // Park: +1 star/turn i punkty. Punkty jeśli liczysz osobno → ScoreSystem.
            CitySystem::addCityParkCount(game, cityId);
        } break;

        case CityUpgradeChoice::SuperUnit: {
            // Super Unit: can only spawn if the city tile is free.
            Map& map = game.getMap();

            UnitId uid=map.unitOn(pos);

            if (uid != Map::kNoUnit) {
                MovementSystem::forceMove(game,uid,pos);
            }

            UnitSpawnSystem::spawnUnit(game,map,UnitType::Giant,owner,pos,false);


        } break;
        default:
            return false;
    }

    return true;
}