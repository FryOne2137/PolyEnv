//
// Created by Fryderyk Niedzwiecki on 03/02/2026.
//

#include "LighthouseSystem.h"

#include "Game.h"
#include "World/Map.h"
#include "World/Tile.h"
#include "Systems/CitySystem.h"
#include "Systems/MonumentSystem.h"
#include "Systems/PlayerSystem.h"

static inline int mapMaxCoord(const Map& map) {
    return map.getWidth() - 1;
}

bool LighthouseSystem::isLighthousePos(const Game& game, Pos p) {
    const Map& map = game.getMap();
    const int max = mapMaxCoord(map);
    return ( (p.x == 0   && p.y == 0)   ||
             (p.x == 0   && p.y == max) ||
             (p.x == max && p.y == 0)   ||
             (p.x == max && p.y == max) );
}

uint8_t LighthouseSystem::lighthouseIndex(const Game& game, Pos p) {
    const Map& map = game.getMap();
    const int max = mapMaxCoord(map);

    if (p.x == 0   && p.y == 0)   return 0;
    if (p.x == 0   && p.y == max) return 1;
    if (p.x == max && p.y == 0)   return 2;
    return 3; // (max, max)
}

bool LighthouseSystem::onTileRevealed(Game& game, PlayerId pid, Pos p) {
    if (!game.getMap().inBounds(p)) return false;
    if (pid == kNoPlayer) return false;
    if (!isLighthousePos(game, p)) return false;

    const uint8_t idx = lighthouseIndex(game, p);

    // First discovery of this lighthouse by this player
    const bool firstTimeForThisPlayer = game.markLighthouseDiscovered(idx, pid);
    if (!firstTimeForThisPlayer) return false;

    // ---- Reward: +1 population ----
    {
        CityId targetCid = kNoCity;

        // Prefer capital
        const CityId capId = PlayerSystem::getCapitalId(game, pid);
        if (capId != kNoCity && CitySystem::cityExists(game, capId)) {
            targetCid = capId;
        }

        // Fallback: highest level, then highest population
        if (targetCid == kNoCity) {
            CityId bestCid = kNoCity;
            for (CityId cid : PlayerSystem::getCities(game, pid)) {
                if (cid == kNoCity) continue;
                if (!CitySystem::cityExists(game, cid)) continue;

                if (bestCid == kNoCity) {
                    bestCid = cid;
                    continue;
                }

                const uint8_t lvl  = CitySystem::getCityLevel(game, cid);
                const uint8_t blvl = CitySystem::getCityLevel(game, bestCid);
                if (lvl > blvl) {
                    bestCid = cid;
                    continue;
                }

                if (lvl == blvl) {
                    const int16_t pop  = CitySystem::getCityPopulation(game, cid);
                    const int16_t bpop = CitySystem::getCityPopulation(game, bestCid);
                    if (pop > bpop) {
                        bestCid = cid;
                    }
                }
            }
            targetCid = bestCid;
        }

        if (targetCid != kNoCity) {
            const uint8_t oldLevel = CitySystem::getCityLevel(game, targetCid);

            (void)CitySystem::addPopulation(game, targetCid, 1);

            const uint8_t newLevel = CitySystem::getCityLevel(game, targetCid);
            if (oldLevel < 5 && newLevel >= 5) {
                MonumentSystem::onCityReachedLevel5(game, CitySystem::getCityPos(game, targetCid));
            }
        }
    }

    // ---- Monument: Eye of God (4 lighthouses) ----
    int lighthouseCount = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        if (game.hasPlayerDiscoveredLighthouse(i, pid)) {
            ++lighthouseCount;
        }
    }

    MonumentSystem::onLighthouseCountUpdated(game, pid, lighthouseCount);

    return true;
}

uint16_t LighthouseSystem::getDiscoveredByMask(const Game& game, uint8_t lighthouseIdx) {
    return game.getLighthouseDiscoveredByMask(lighthouseIdx);
}

bool LighthouseSystem::hasPlayerDiscovered(const Game& game, uint8_t lighthouseIdx, PlayerId pid) {
    return game.hasPlayerDiscoveredLighthouse(lighthouseIdx, pid);
}