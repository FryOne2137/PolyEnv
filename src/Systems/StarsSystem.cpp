//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#include "StarsSystem.h"

#include <algorithm>

#include "Game.h"
#include "Systems/CitySystem.h"
#include "Systems/PlayerSystem.h"
#include "World/Map.h"
#include "World/Tile.h"
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

    bool hasForge = false;
    bool hasSawmill = false;
    bool hasWindmill = false;
    bool hasPort = false;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Pos p{marketPos.x + dx, marketPos.y + dy};
            if (!map.inBounds(p)) continue;
            const Tile& t = map.at(p);
            if (!tileInPlayersEmpire(game, pid, t)) continue;

            const auto b = t.getBuildingType();
            if (b == BuildingTypeEnum::Forge) hasForge = true;
            else if (b == BuildingTypeEnum::Sawmill) hasSawmill = true;
            else if (b == BuildingTypeEnum::Windmill) hasWindmill = true;
            else if (b == BuildingTypeEnum::Port) hasPort = true;
        }
    }

    int unique = 0;
    if (hasForge) ++unique;
    if (hasSawmill) ++unique;
    if (hasWindmill) ++unique;

    // Current Polytopia (Path of the Ocean):
    // 2 stars per UNIQUE adjacent production building type (Forge/Sawmill/Windmill).
    // Doubled if adjacent to a Port.
    int spt = 2 * unique;
    if (hasPort && spt > 0) spt *= 2;

    // Max possible is Forge+Sawmill+Windmill (=6), doubled by Port (=12).
    return std::clamp(spt, 0, 12);
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
        income += static_cast<int>(CitySystem::getCityStarsPerRound(game, cid));
        income += marketIncomeForCity(game, pid, cid);
    }

    return std::max(0, income);
}

void StarsSystem::applyIncomeForPlayer(Game& game, PlayerId pid) {
    int income = 0;

    for (CityId cid : PlayerSystem::getCities(game, pid)) {
        if (!CitySystem::cityExists(game, cid)) continue;

        // No income if city is under siege (enemy unit inside the city)
        if (CitySystem::isCityUnderSiege(game, cid)) {
            continue;
        }

        // No income if city was infiltrated; clear infiltration after income collection attempt.
        if (CitySystem::getCityIsInfiltrated(game, cid)) {
            (void)CitySystem::setCityIsInfiltrated(game, cid, false);
            continue;
        }

        income += static_cast<int>(CitySystem::getCityStarsPerRound(game, cid));
        income += marketIncomeForCity(game, pid, cid);
    }

    if (income > 0) {
        PlayerSystem::addStars(game, pid, income);
    }
}