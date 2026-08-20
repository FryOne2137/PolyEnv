//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "VisionSystem.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
#include <vector>

#include "../game/Game.h"
#include "ScoreSystem.h"
#include "systems/LighthouseSystem.h"
#include "systems/CitySystem.h" // provides CitySystem
#include "systems/UnitSystem.h" // provides UnitSystem
#include "systems/PlayerSystem.h" // provides PlayerSystem
#include "world/Map.h"
#include "world/Tile.h"
#include "terrain/BaseTerrainEnum.h"
#include "terrain/VisibilityEnum.h"

namespace {

void awardMeetingIfFirst(Game& game, PlayerId observer, PlayerId discovered) {
    if (observer == kNoPlayer || discovered == kNoPlayer || observer == discovered) return;
    if (!PlayerSystem::playerExists(game, observer) ||
        !PlayerSystem::playerExists(game, discovered)) return;
    if (!PlayerSystem::markMet(game, observer, discovered)) return;

    const int discoveredScore = ScoreSystem::getScore(game, discovered);
    PlayerSystem::addStars(game, observer, VisionSystem::meetingReward(discoveredScore));
}

bool unitHasActivatedHide(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) return false;
    if (!UnitSystem::hasSkill(game, unitId, UnitSkill::Hide)) return false;
    const Pos lastMove = UnitSystem::getLastMoveDir(game, unitId);
    return lastMove.x != 0 || lastMove.y != 0;
}

void discoverPlayerAtTile(Game& game, PlayerId observer, Pos pos) {
    const Map& map = game.getMap();
    if (!map.inBounds(pos)) return;

    const Tile& tile = map.at(pos);
    const UnitId unitId = map.unitOn(pos);
    if (unitId != Map::kNoUnit && UnitSystem::unitExists(game, unitId) &&
        !unitHasActivatedHide(game, unitId)) {
        awardMeetingIfFirst(game, observer, UnitSystem::getOwnerId(game, unitId));
    }

    if (tile.getSettlementType() != SettlementTypeEnum::City) return;
    const CityId cityId = CitySystem::resolveCityIdForTile(game, tile);
    if (!CitySystem::cityExists(game, cityId)) return;
    awardMeetingIfFirst(
        game, observer, static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId)));
}

bool playerIsCurrentlyVisibleTo(const Game& game, PlayerId observer, PlayerId target) {
    for (UnitId targetUnit : PlayerSystem::getUnits(game, target)) {
        if (!UnitSystem::unitExists(game, targetUnit)) continue;
        if (UnitSystem::getHealth(game, targetUnit) <= 0) continue;
        if (unitHasActivatedHide(game, targetUnit)) continue;
        if (game.isTileVisibleForPlayer(observer, UnitSystem::getPos(game, targetUnit))) {
            return true;
        }
    }

    for (CityId targetCity : PlayerSystem::getCities(game, target)) {
        if (!CitySystem::cityExists(game, targetCity)) continue;
        if (game.isTileVisibleForPlayer(observer, CitySystem::getCityPos(game, targetCity))) {
            return true;
        }
    }

    return false;
}

} // namespace

// Local helper for explorer fog checks
bool VisionSystem::isFogForPlayer(const Tile& t, PlayerIndex idx) {
    return !isRevealed(t.getVisibility(), idx);
}

// Local helper for explorer lighthouse detection
bool VisionSystem::tileHasLighthouse(const Tile& t) {
    return t.getBuildingType() == BuildingTypeEnum::Lighthouse;
}

void VisionSystem::revealFromUnit(Game& game, UnitId uid) {
    if (!UnitSystem::unitExists(game, uid)) return;

    const PlayerId pid = UnitSystem::getOwnerId(game, uid);
    const Pos p = UnitSystem::getPos(game, uid);

    int r = UnitSystem::getVisionRange(game, uid);
    const Tile& t = game.getMap().at(p);
    if (t.getBaseTerrain() == BaseTerrainEnum::Mountain) {
        r = std::max(r, 2);
    }

    revealArea(game, pid, p, r, RevealSource::Unit);
}


void VisionSystem::revealForPlayerFromUnits(Game& game, PlayerId playerId) {
    // Dodajemy widoczność na bazie wszystkich jednostek gracza.
    // UWAGA: To nie "czyści" fog-of-war (czyli nie usuwa widoczności).
    const auto& units = PlayerSystem::getUnits(game, playerId);

    for (UnitId uid : units) {
        // Use revealFromUnit so mountain bonus (range 2) is applied consistently.
        revealFromUnit(game, uid);
    }
}

void VisionSystem::revealArea(
    Game& game,
    PlayerId pid,
    Pos center,
    int range,
    RevealSource source
) {
    Map& map = game.getMap();

    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            Pos p{center.x + dx, center.y + dy};
            if (!map.inBounds(p)) continue;

            Tile& t = map.at(p);

            // ✅ podczas Initial nie odsłaniamy rogów
            if (source == RevealSource::Initial && isCornerTile(map, p)) {
                continue;
            }

            const bool wasFog = isFogForPlayer(t, static_cast<PlayerIndex>(pid));

            VisibilityEnum v = t.getVisibility();
            reveal(v, static_cast<PlayerIndex>(pid));
            t.setVisibility(v);
            game.noteTileVisible(pid, p);

            // Meeting discovery is about seeing an opponent now, not about
            // whether the terrain tile itself was revealed for the first time.
            // Only the active player discovers opponents during their actions;
            // reciprocal discovery is resolved from live vision at end of turn.
            if (source != RevealSource::Initial && pid == game.getCurrentPlayerId()) {
                discoverPlayerAtTile(game, pid, p);
            }

            if (!wasFog) continue;

            // 🔒 BLOKADA: start gry nic nie odpala
            if (source == RevealSource::Initial)
                continue;

            // ✅ UNIT + EXPLORER
            LighthouseSystem::onTileRevealed(game, pid, p);

        }
    }
}

void VisionSystem::resolveCurrentMeetingsFor(Game& game, PlayerId observer) {
    if (!PlayerSystem::playerExists(game, observer)) return;

    const size_t playerCount = game.getPlayers().size();
    for (size_t targetIndex = 0; targetIndex < playerCount; ++targetIndex) {
        const PlayerId target = static_cast<PlayerId>(targetIndex);
        if (target == observer || PlayerSystem::hasMet(game, observer, target)) continue;
        if (!playerIsCurrentlyVisibleTo(game, observer, target)) continue;
        awardMeetingIfFirst(game, observer, target);
    }
}

void VisionSystem::resolveEndTurnMeetings(Game& game, PlayerId activePlayer) {
    if (!PlayerSystem::playerExists(game, activePlayer)) return;

    // Covers opponents that became visible without revealing new terrain.
    resolveCurrentMeetingsFor(game, activePlayer);

    const size_t playerCount = game.getPlayers().size();
    for (size_t observerIndex = 0; observerIndex < playerCount; ++observerIndex) {
        const PlayerId observer = static_cast<PlayerId>(observerIndex);
        if (observer == activePlayer || PlayerSystem::hasMet(game, observer, activePlayer)) continue;
        if (!playerIsCurrentlyVisibleTo(game, observer, activePlayer)) continue;
        awardMeetingIfFirst(game, observer, activePlayer);
    }
}



// Explorer reward logic (see Explorer wiki page).
// Replace the stub or body of doExplorer with this implementation.
// This function must be placed in the appropriate .cpp file where other gameplay actions are implemented.
// If a declaration exists in a header, do not modify it.
void VisionSystem::doExplorer(Game& game, PlayerId pid, Pos start) // <-- adapt signature to match project, do NOT change existing signature!
{
    // ---- Resolve required inputs (pid + start position) before running ----
    // NOTE: the surrounding function signature must already provide or be able to resolve these.
    // Make sure you end up with:
    //   PlayerId pid;
    //   Pos pos;

    auto& map = game.getMap();

    // Tech gating for terrain
    //const Player& pl = game.getPlayer(pid);

    auto canEnter = [&](Pos p) -> bool {
        if (!map.inBounds(p)) return false;
        const Tile& t = map.at(p);
        const auto bt = t.getBaseTerrain();

        // Default: land is always ok
        if (bt == BaseTerrainEnum::Mountain) {
            // Requires Climbing
            // If TechId naming differs, adapt to the project enum.
            return PlayerSystem::hasTech(game, pid, TechId::Climbing);
        }
        if (bt == BaseTerrainEnum::Water) {
            // Requires Sailing
            return PlayerSystem::hasTech(game, pid, TechId::Sailing);
        }
        if (bt == BaseTerrainEnum::Ocean) {
            // Requires Navigation
            return PlayerSystem::hasTech(game, pid, TechId::Navigation);
        }
        return true;
    };

    auto revealExplorerPlus = [&](Pos center) {
        constexpr int kDirs = 5;
        const int dx[kDirs] = {0, 1, -1, 0, 0};
        const int dy[kDirs] = {0, 0, 0, 1, -1};

        for (int i = 0; i < kDirs; ++i) {
            Pos p{center.x + dx[i], center.y + dy[i]};
            if (!map.inBounds(p)) continue;

            // ✅ jednolite reveal (latarnia + meeting + blokada Initial)
            revealArea(game, pid, p, 0, RevealSource::Explorer);
        }
    };


    auto countFogClearedFrom = [&](Pos center, bool& outHasLighthouse) -> int {
        outHasLighthouse = false;
        int cnt = 0;

        constexpr int kDirs = 5;
        const int dx[kDirs] = {0, 1, -1, 0, 0};
        const int dy[kDirs] = {0, 0, 0, 1, -1};

        for (int i = 0; i < kDirs; ++i) {
            Pos p{center.x + dx[i], center.y + dy[i]};
            if (!map.inBounds(p)) continue;
            const Tile& tt = map.at(p);
            if (isFogForPlayer(tt, static_cast<PlayerIndex>(pid))) {
                ++cnt;
                if (tileHasLighthouse(tt)) outHasLighthouse = true;
            }
        }

        // Cap 5 to 4 (as described)
        if (cnt >= 4) cnt = 4;
        return cnt;
    };

    // 8-neighborhood movement (supports diagonal preference described in the wiki)
    static const int ndx[8] = { 1, -1,  0,  0,  1,  1, -1, -1};
    static const int ndy[8] = { 0,  0,  1, -1,  1, -1,  1, -1};

    // Deterministic RNG for tie-breaking (still "random" but stable per turn/player/pos)
    const uint32_t seed = static_cast<uint32_t>(game.getTurnNumber()) * 2654435761u
                        ^ (static_cast<uint32_t>(pid) + 1u) * 2246822519u
                        ^ (static_cast<uint32_t>(start.x & 0xFFFF) << 16)
                        ^ (static_cast<uint32_t>(start.y & 0xFFFF));
    std::mt19937 rng(seed);

    // Reveal starting area
    Pos pos = start;
    revealExplorerPlus(pos);

    Pos prev = pos;
    bool hasPrev = false;

    // ---- 15 moves ----
    for (int step = 0; step < 15; ++step) {
        // BFS from current pos up to distance 4 to find reachable tiles (ignoring units)
        const int w = map.getWidth();
        const int h = map.getHeight();
        const int N = w * h;

        std::vector<int> dist(static_cast<size_t>(N), -1);
        auto idxOf = [&](Pos p) -> int { return p.y * w + p.x; };

        std::queue<Pos> q;
        dist[static_cast<size_t>(idxOf(pos))] = 0;
        q.push(pos);

        while (!q.empty()) {
            Pos cur = q.front();
            q.pop();
            const int cd = dist[static_cast<size_t>(idxOf(cur))];
            if (cd >= 4) continue;

            for (int i = 0; i < 8; ++i) {
                Pos np{cur.x + ndx[i], cur.y + ndy[i]};
                if (!map.inBounds(np)) continue;
                if (!canEnter(np)) continue;

                const int ni = idxOf(np);
                if (dist[static_cast<size_t>(ni)] != -1) continue;
                dist[static_cast<size_t>(ni)] = cd + 1;
                q.push(np);
            }
        }

        // Seed score map from tiles that can clear fog (within dist<=4)
        // Lower score is better.
        const int kInf = 1000;
        std::vector<int> score(static_cast<size_t>(N), kInf);

        std::queue<std::pair<Pos,int>> bfs;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Pos p{x, y};
                const int di = dist[static_cast<size_t>(idxOf(p))];
                if (di < 0 || di > 4) continue;
                if (!canEnter(p)) continue;

                bool hasLh = false;
                const int fogCnt = countFogClearedFrom(p, hasLh);
                if (fogCnt <= 0) continue;

                // Base scores from wiki examples:
                // 143=4 fog, 153=3, 163=2, 173=1; lighthouse bonus -33.
                int s = 183 - 10 * fogCnt;
                if (hasLh) s -= 33;

                const int pi = idxOf(p);
                if (s < score[static_cast<size_t>(pi)]) {
                    score[static_cast<size_t>(pi)] = s;
                    bfs.push({p, 0});
                }
            }
        }

        // BFS propagate scores outward, +100 per layer, capped at 3 iterations
        while (!bfs.empty()) {
            auto [cur, depth] = bfs.front();
            bfs.pop();
            if (depth >= 3) continue;

            const int baseS = score[static_cast<size_t>(idxOf(cur))];

            for (int i = 0; i < 8; ++i) {
                Pos np{cur.x + ndx[i], cur.y + ndy[i]};
                if (!map.inBounds(np)) continue;
                if (!canEnter(np)) continue;

                const int ni = idxOf(np);
                const int ns = baseS + 100;
                if (ns < score[static_cast<size_t>(ni)]) {
                    score[static_cast<size_t>(ni)] = ns;
                    bfs.push({np, depth + 1});
                }
            }
        }

        // Choose the best adjacent step.
        std::vector<Pos> best;
        int bestScore = kInf;

        for (int i = 0; i < 8; ++i) {
            Pos np{pos.x + ndx[i], pos.y + ndy[i]};
            if (!map.inBounds(np)) continue;
            if (!canEnter(np)) continue;

            int s = score[static_cast<size_t>(idxOf(np))];

            // If no fog scores exist, all adjacent tiles are equal except backtrack penalty.
            if (s == kInf) s = 500;

            // Avoid direct backtracking with a small penalty.
            if (hasPrev && np.x == prev.x && np.y == prev.y) {
                s += 5;
            }

            if (s < bestScore) {
                bestScore = s;
                best.clear();
                best.push_back(np);
            } else if (s == bestScore) {
                best.push_back(np);
            }
        }

        // No available steps at all => go poof
        if (best.empty()) {
            break;
        }

        // Random tie-break among equals
        std::uniform_int_distribution<size_t> pick(0, best.size() - 1);
        Pos next = best[pick(rng)];

        prev = pos;
        hasPrev = true;
        pos = next;

        revealExplorerPlus(pos);
    }
}

int VisionSystem::countRevealedTiles(const Game& game, PlayerId playerId) {
    const Map& map = game.getMap();
    const PlayerIndex idx = static_cast<PlayerIndex>(playerId);

    int count = 0;
    for (const Tile& t : map.allTiles()) {
        if (isRevealed(t.getVisibility(), idx)) {
            ++count;
        }
    }
    return count;
}

int VisionSystem::meetingReward(int enemyScore) {
    const int thousands = (enemyScore + 999) / 1000; // ceil
    int stars = 1 + 2 * thousands;
    return std::min(stars, 11);
}

 bool VisionSystem::isCornerTile(const Map& map, Pos p) {
    const int w = map.getWidth();
    const int h = map.getHeight();
    return (p.x == 0 && p.y == 0) ||
           (p.x == 0 && p.y == h - 1) ||
           (p.x == w - 1 && p.y == 0) ||
           (p.x == w - 1 && p.y == h - 1);
}

void VisionSystem::revealTile(Game& game, PlayerId pid, Pos p, RevealSource source) {
    VisionSystem::revealArea(game, pid, p, 0, source);
}
