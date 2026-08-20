//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#include "StarsSystem.h"

#include <algorithm>

#include "../game/Game.h"
#include "systems/BuildingSystem.h"
#include "systems/CitySystem.h"
#include "systems/PlayerSystem.h"
#include "world/Map.h"
#include "world/Tile.h"
#include "terrain/BuildingTypeEnum.h"

namespace {

static inline bool tileInPlayersEmpire(const Game& game, PlayerId pid, const Tile& t) {
    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return false;
    if (!CitySystem::cityExists(game, cid)) return false;
    return static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) == pid;
}

static int marketIncomeAt(const Game& game, PlayerId pid, Pos marketPos) {
    const Map& map = game.getMap();
    if (!map.inBounds(marketPos)) return 0;

    const Tile& mt = map.at(marketPos);
    if (mt.getBuildingType() != BuildingTypeEnum::Market) return 0;
    if (!tileInPlayersEmpire(game, pid, mt)) return 0;

    // A Market produces one star for every level of every adjacent Sawmill,
    // Windmill and Forge. BuildingSystem already computes that sum and caps
    // the Market at level 8. Ports do not modify this income.
    return static_cast<int>(BuildingSystem::getBuildingLevel(game, pid, marketPos));
}

} // namespace

int StarsSystem::marketIncomeForCity(const Game& game, PlayerId pid, CityId cid) {
    if (cid == kNoCity) return 0;
    if (!CitySystem::cityExists(game, cid)) return 0;

    const Map& map = game.getMap();
    const int W = map.getWidth();
    const int H = map.getHeight();

    int spt = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Pos p{x, y};
            const Tile& t = map.at(p);
            if (t.getTerritoryCityId() != cid) continue;
            if (t.getBuildingType() != BuildingTypeEnum::Market) continue;
            spt += marketIncomeAt(game, pid, p);
        }
    }

    return std::max(0, spt);
}

int StarsSystem::calcIncomeForPlayer(const Game& game, PlayerId pid) {
    int income = 0;

    for (CityId cid : PlayerSystem::getCities(game, pid)) {
        if (!CitySystem::cityExists(game, cid)) continue;

        // A besieged city produces no income, including independent sources
        // located in its territory.
        if (CitySystem::isCityUnderSiege(game, cid)) continue;

        // Infiltration blocks only normal city income. Markets keep working.
        if (!CitySystem::getCityIsInfiltrated(game, cid)) {
            income += static_cast<int>(CitySystem::getCityStarsPerRound(game, cid));
        }
        income += marketIncomeForCity(game, pid, cid);
    }

    return std::max(0, income);
}

void StarsSystem::applyIncomeForPlayer(Game& game, PlayerId pid) {
    int income = 0;

    for (CityId cid : PlayerSystem::getCities(game, pid)) {
        if (!CitySystem::cityExists(game, cid)) continue;

        // The status always expires in the income phase at the start of the
        // city's owner's next turn, even if the city is now under siege.
        const bool infiltrated = CitySystem::getCityIsInfiltrated(game, cid);
        if (infiltrated) {
            (void)CitySystem::setCityIsInfiltrated(game, cid, false);
        }

        // A besieged city produces no income at all.
        if (CitySystem::isCityUnderSiege(game, cid)) continue;

        // Infiltration blocks only normal city income; Markets are separate.
        if (!infiltrated) {
            income += static_cast<int>(CitySystem::getCityStarsPerRound(game, cid));
        }
        income += marketIncomeForCity(game, pid, cid);
    }

    if (income > 0) {
        PlayerSystem::addStars(game, pid, income);
    }
}
