//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#include "InfiltrationSystem.h"

#include <array>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>

#include "../game/Game.h"
#include "world/Map.h"
#include "world/Tile.h"

#include "systems/PlayerSystem.h"
#include "systems/CitySystem.h"
#include "systems/UnitSystem.h"
#include "systems/UnitSpawnSystem.h"

#include "../content/tech/TechDB.h"

namespace {

static inline bool isWater(const Tile& t) {
    const auto bt = t.getBaseTerrain();
    return bt == BaseTerrainEnum::Water || bt == BaseTerrainEnum::Ocean;
}

static inline bool isMountain(const Tile& t) {
    return t.getBaseTerrain() == BaseTerrainEnum::Mountain;
}

static inline bool isForest(const Tile& t) {
    return t.getBaseTerrain() == BaseTerrainEnum::Forest;
}

// NOTE: This is a simplified approximation of the wiki behavior:
// "prioritize tiles that provide a defense bonus" (mountain/forest depending on tech).
static inline int defenseBonusScore(const Game& game, PlayerId attacker, const Tile& t) {
    int s = 0;
    if (isForest(t) && PlayerSystem::hasTech(game, attacker, TechId::Archery)) s += 50;
    if (isMountain(t) && PlayerSystem::hasTech(game, attacker, TechId::Climbing)) s += 50;
    if (isWater(t) && PlayerSystem::hasTech(game, attacker, TechId::Aquatism)) s += 50;
    // Water tiles are only used when no land tiles are available (Pirate spawn). We don't bias them much.
    return s;
}

// Best-K selection for K<=5 without sorting (O(N*K)).
struct Cand {
    int score = -1;
    int idx = -1;
};

static inline void pushBest(std::array<Cand, 5>& best, int& bestCount, int score, int idx) {
    // Skip duplicates.
    for (int i = 0; i < bestCount; ++i) {
        if (best[static_cast<size_t>(i)].idx == idx) return;
    }

    Cand c{score, idx};
    if (bestCount < 5) {
        best[static_cast<size_t>(bestCount++)] = c;
    } else {
        // Replace worst if better.
        int worstI = 0;
        for (int i = 1; i < 5; ++i) {
            if (best[static_cast<size_t>(i)].score < best[static_cast<size_t>(worstI)].score) worstI = i;
        }
        if (c.score <= best[static_cast<size_t>(worstI)].score) return;
        best[static_cast<size_t>(worstI)] = c;
    }

    // Keep array sorted descending by score for deterministic selection.
    for (int i = bestCount - 1; i > 0; --i) {
        if (best[static_cast<size_t>(i)].score > best[static_cast<size_t>(i - 1)].score) {
            std::swap(best[static_cast<size_t>(i)], best[static_cast<size_t>(i - 1)]);
        } else {
            break;
        }
    }
}

// Minimal destroy that keeps the rest of the engine consistent enough for spawning.
// (Hard delete from vectors is not supported; we detach from map + owner lists.)
static void detachUnit(Game& game, UnitId uid) {
    if (!UnitSystem::unitExists(game, uid)) return;

    const PlayerId owner = UnitSystem::getOwnerId(game, uid);
    const Pos pos = UnitSystem::getPos(game, uid);

    // Clear map occupancy.
    if (game.getMap().inBounds(pos) && game.getMap().unitOn(pos) == uid) {
        game.getMap().setUnitOn(pos, Map::kNoUnit);
    }

    // Remove from player's unit list and from any of owner's cities.
    if (owner != kNoPlayer) {
        PlayerSystem::removeUnit(game, owner, uid);

        // Best-effort: remove from any of owner's cities (if your CitySystem tracks this list).
        const auto& cities = PlayerSystem::getCities(game, owner);
        for (CityId cid : cities) {
            (void)CitySystem::removeUnitFromCity(game, uid, cid);
        }
    }

    // Mark as dead/detached.
    (void)UnitSystem::setHealth(game, uid, 0);
    (void)UnitSystem::setOwnerId(game, uid, kNoPlayer);
    (void)UnitSystem::setPos(game, uid, Pos{-9999, -9999});
}


} // namespace


bool InfiltrationSystem::infiltrateCity(Game& game, UnitId cloakId, CityId cityId) {
    if (!UnitSystem::unitExists(game, cloakId)) return false;
    if (!CitySystem::cityExists(game, cityId)) return false;

    Map& map = game.getMap();

    const PlayerId attacker = UnitSystem::getOwnerId(game, cloakId);
    if (attacker == kNoPlayer) return false;

    const PlayerId defender = static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId));
    if (defender == kNoPlayer) return false;
    if (attacker == defender) return false;

    // Blockade 1: cannot infiltrate a city that is under siege (enemy unit on city center).
    // IMPORTANT: when infiltration is triggered by MOVING onto the city tile, the infiltrator itself
    // temporarily occupies the city center. That should NOT count as "siege" for this infiltration.
    {
        const UnitId occ = map.unitOn(CitySystem::getCityPos(game, cityId));
        if (occ != Map::kNoUnit && UnitSystem::unitExists(game, occ)) {
            // If the occupant belongs to the defender, the city is under siege.
            // If the occupant is some other attacker unit (not this cloak), also treat as siege.
            const PlayerId occOwner = UnitSystem::getOwnerId(game, occ);
            if (occOwner == defender || (occOwner != kNoPlayer && occOwner != defender && occ != cloakId)) {
                std::cout << "[infiltrate] blocked: city under siege cityId=" << int(cityId) << "\n";
                return false;
            }
        }
    }

    // Blockade 2: cannot infiltrate again until the owner's next turn has cleared the infiltration flag.
    if (CitySystem::getCityIsInfiltrated(game, cityId)) {
        std::cout << "[infiltrate] blocked: cooldown (city already infiltrated) cityId=" << int(cityId) << "\n";
        return false;
    }

    const Pos cityPos = CitySystem::getCityPos(game, cityId);
    if (!map.inBounds(cityPos)) return false;

    // Cloak must be adjacent to the city tile.
    {
        const Pos cp = UnitSystem::getPos(game, cloakId);
        const int dx = std::abs(cp.x - cityPos.x);
        const int dy = std::abs(cp.y - cityPos.y);
        if (dx > 1 || dy > 1) return false;

        // 0) Steal income immediately and make the city produce 0 stars on defender's next turn.
        // This only affects the city's normal income, not other star sources.
        const int income = static_cast<int>(CitySystem::getCityStarsPerRound(game, cityId));
        if (income > 0) {
            PlayerSystem::addStars(game, attacker, income);
        }
        CitySystem::blockCityIncomeNextOwnerTurn(game, cityId);
    }

    // 1) Damage unit in city center (if any) by cloak's base attack at full health.
    {
        const UnitId occ = map.unitOn(cityPos);
        if (occ != Map::kNoUnit && UnitSystem::unitExists(game, occ)) {
            // Only damage enemy unit.
            if (UnitSystem::getOwnerId(game, occ) == defender) {
                (void)UnitSystem::applyDamage(game, occ, 2);
                if (UnitSystem::getHealth(game, occ) <= 0) {
                    detachUnit(game, occ);
                }
            }
        }
    }

    // 2) Consume cloak.
    detachUnit(game, cloakId);

    // Mark cooldown until defender's next turn clears the infiltration flag.
    (void)CitySystem::setCityIsInfiltrated(game, cityId, true);

    // 3) Spawn Daggers/Pirates.
    // Wiki: count == city level, capped at 5.
    const int level = static_cast<int>(CitySystem::getCityLevel(game, cityId));
    const int want = std::clamp(level, 0, 5);
    if (want <= 0) return true;

    // Collect candidates in ONE scan (still O(W*H), but allocation-free-ish and deterministic).
    // We keep separate land/water best sets; water is used only if land is insufficient.
    std::array<Cand, 5> bestLand{};
    std::array<Cand, 5> bestWater{};
    int bestLandCount = 0;
    int bestWaterCount = 0;

    const int W = map.getWidth();
    const int H = map.getHeight();

    // Precompute city tile index for strong priority.
    const int cityIdx = map.index(cityPos);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Pos p{x, y};
            const Tile& t = map.at(p);

            if (t.getTerritoryCityId() != cityId) continue;

            // Must be empty.
            if (map.unitOn(p) != Map::kNoUnit) continue;

            // Score: prioritize city tile, then defense bonus tiles.
            int score = 0;
            const int idx = map.index(p);
            if (idx == cityIdx) score += 1000;
            score += defenseBonusScore(game, attacker, t);

            // Mild deterministic bias: prefer "southern" tiles (bigger y), then x.
            // This keeps behavior stable across platforms and avoids random.
            score += (y * 2) + (x > 0 ? (x & 1) : 0);

            if (isWater(t)) {
                pushBest(bestWater, bestWaterCount, score, idx);
            } else {
                pushBest(bestLand, bestLandCount, score, idx);
            }
        }
    }

    auto spawnAtIdx = [&](int idx, UnitType ut) -> bool {
        const int x = idx % W;
        const int y = idx / W;
        const Pos p{x, y};
        if (!map.inBounds(p)) return false;
        if (map.unitOn(p) != Map::kNoUnit) return false;

        const UnitId nu = UnitSpawnSystem::spawnUnitForced(
            game, map, ut, attacker, p,
            /*canActImmediately=*/false,
            /*makeVeteran=*/false
        );
        return nu != kNoUnit;
    };

    int spawned = 0;

    // Prefer the city center tile as the very first spawn location (if available).
    // This matters especially when infiltration was triggered by moving into an empty city.
    if (spawned < want) {
        const Tile& ct = map.at(cityPos);
        if (map.unitOn(cityPos) == Map::kNoUnit) {
            if (isWater(ct)) {
                if (spawnAtIdx(cityIdx, UnitType::Pirate)) {
                    ++spawned;
                }
            } else {
                if (spawnAtIdx(cityIdx, UnitType::Dagger)) {
                    ++spawned;
                }
            }
        }
    }

    // First, spawn on best land candidates as Daggers.
    for (int i = 0; i < bestLandCount && spawned < want; ++i) {
        const int idx = bestLand[static_cast<size_t>(i)].idx;
        if (idx < 0) continue;
        if (spawnAtIdx(idx, UnitType::Dagger)) {
            ++spawned;
        }
    }

    // If still missing, use water candidates as Pirates.
    for (int i = 0; i < bestWaterCount && spawned < want; ++i) {
        const int idx = bestWater[static_cast<size_t>(i)].idx;
        if (idx < 0) continue;
        if (spawnAtIdx(idx, UnitType::Pirate)) {
            ++spawned;
        }
    }

    (void)spawned;
    return true;
}