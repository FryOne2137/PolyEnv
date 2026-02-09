//
// Created by Fryderyk Niedzwiecki on 03/02/2026.
//

#include "LighthouseSystem.h"

#include <vector>

#include "../game/Game.h"
#include "world/Map.h"
#include "world/Tile.h"
#include "systems/CitySystem.h"
#include "systems/MonumentSystem.h"
#include "systems/PlayerSystem.h"

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

bool LighthouseSystem::hasPlayerDiscovered(const Game& game, PlayerId pid, LighthouseCorner corner) {
    if (pid == kNoPlayer) return false;

    const Map& map = game.getMap();
    const int max = map.getWidth() - 1;

    Pos p;
    switch (corner) {
        case LighthouseCorner::TopLeft:     p = Pos{0, 0}; break;
        case LighthouseCorner::BottomLeft:  p = Pos{0, max}; break;
        case LighthouseCorner::TopRight:    p = Pos{max, 0}; break;
        case LighthouseCorner::BottomRight: p = Pos{max, max}; break;
    }
    const Tile& t = game.getMap().at(p);

    const VisibilityEnum vis = t.getVisibility();
    const uint16_t mask = static_cast<uint16_t>(vis);

    const uint32_t idx = static_cast<uint32_t>(pid);
    if (idx >= 16) return false;

    return (mask & (uint16_t(1) << idx)) != 0;
}

static inline Pos cornerPos(const Game& game, LighthouseCorner c) {
    const Map& map = game.getMap();
    const int max = map.getWidth() - 1; // albo mapMaxCoord(map)
    switch (c) {
        case LighthouseCorner::TopLeft:     return Pos{0, 0};
        case LighthouseCorner::BottomLeft:  return Pos{0, max};
        case LighthouseCorner::TopRight:    return Pos{max, 0};
        case LighthouseCorner::BottomRight: return Pos{max, max};
    }
    return Pos{0,0}; // żeby kompilator nie marudził
}


static inline std::vector<PlayerId> getCornerDiscoveredPlayers(const Game& game, LighthouseCorner c) {
    std::vector<PlayerId> out;

    const Pos p = cornerPos(game, c);
    const Tile& t = game.getMap().at(p);

    const VisibilityEnum vis = t.getVisibility();
    const uint16_t mask = static_cast<uint16_t>(vis);

    for (uint8_t pid = 0; pid < 16; ++pid) {
        if (mask & (uint16_t(1) << pid)) {
            out.push_back(static_cast<PlayerId>(pid));
        }
    }

    return out;
}

static inline bool hasPlayerDiscoveredCorner(const Game& game, LighthouseCorner c, PlayerId pid) {
    if (pid == kNoPlayer) return false;

    const auto players = getCornerDiscoveredPlayers(game, c);
    for (PlayerId p : players) {
        if (p == pid) return true;
    }

    return false;
}
