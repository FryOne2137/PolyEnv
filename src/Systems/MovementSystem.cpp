//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "Systems/MovementSystem.h"

#include "Game.h" // needs Game::getUnit and Game::getMap()

#include <queue>
#include <algorithm>
#include <limits>
#include <array>
#include <unordered_map>

static inline int idx(Pos p, int w) {
    return p.y * w + p.x;
}

static inline Pos fromIdx(int i, int w) {
    return Pos{ i % w, i / w };
}

// 8-neighborhood (square grid with diagonals). This matches the previous behavior where
// "corners" were reachable.
static inline std::array<Pos, 8> neighbors8(Pos cur) {
    return {
        Pos{cur.x + 1, cur.y},
        Pos{cur.x - 1, cur.y},
        Pos{cur.x, cur.y + 1},
        Pos{cur.x, cur.y - 1},
        Pos{cur.x + 1, cur.y + 1},
        Pos{cur.x + 1, cur.y - 1},
        Pos{cur.x - 1, cur.y + 1},
        Pos{cur.x - 1, cur.y - 1}
    };
}

// Polytopia-style movement works best on a hex grid and with half-steps (0.5 = 1 half-point).
// NOTE: This file currently doesn't have access to terrain/road APIs, so the rule hooks below
// default to "no roads" and "no terminal tiles". Wire them to your Map/Tile/Unit data when available.

static inline std::array<Pos, 6> hexNeighborsOddR(Pos cur) {
    // Odd-r horizontal layout (rows are offset). If your coordinate system differs,
    // adjust these deltas accordingly.
    const bool odd = (cur.y & 1) != 0;
    if (!odd) {
        return {
            Pos{cur.x + 1, cur.y},     // E
            Pos{cur.x - 1, cur.y},     // W
            Pos{cur.x,     cur.y - 1}, // NE
            Pos{cur.x - 1, cur.y - 1}, // NW
            Pos{cur.x,     cur.y + 1}, // SE
            Pos{cur.x - 1, cur.y + 1}  // SW
        };
    }
    return {
        Pos{cur.x + 1, cur.y},     // E
        Pos{cur.x - 1, cur.y},     // W
        Pos{cur.x + 1, cur.y - 1}, // NE
        Pos{cur.x,     cur.y - 1}, // NW
        Pos{cur.x + 1, cur.y + 1}, // SE
        Pos{cur.x,     cur.y + 1}  // SW
    };
}

static inline bool isRoadLikeTile(const Game& /*game*/, Pos /*p*/, UnitId /*movingUnit*/) {
    // TODO: Wire to your map tile data.
    // Expected: true if tile has a road OR is a city/village tile that counts as road.
    return false;
}

static inline bool isEnemyTerritoryForRoadBonus(const Game& /*game*/, Pos /*p*/, UnitId /*movingUnit*/) {
    // TODO: Wire to ownership/territory system.
    // Expected: true if road bonus should NOT apply on this tile (enemy territory).
    return false;
}

static inline bool isTerminalAfterEntering(const Game& /*game*/, Pos /*p*/, UnitId /*movingUnit*/) {
    // TODO: Wire to terrain and ZoC rules.
    // Expected: true if after entering p the unit must stop moving this turn
    // (e.g. rough terrain without road, entering ZoC without creep/sneak, etc.).
    return false;
}

static inline int stepCostHalfPoints(const Game& game, UnitId movingUnit, Pos from, Pos to) {
    // Base move cost is 1.0 move point (= 2 half-points).
    // Road move cost is 0.5 (= 1 half-point) when both tiles are road-like AND in friendly/neutral territory.
    const bool fromRoad = isRoadLikeTile(game, from, movingUnit);
    const bool toRoad   = isRoadLikeTile(game, to, movingUnit);
    const bool roadOK   = fromRoad && toRoad && !isEnemyTerritoryForRoadBonus(game, to, movingUnit);
    return roadOK ? 1 : 2;
}

struct DijkstraNode {
    int dist;
    Pos p;
    bool operator>(const DijkstraNode& other) const { return dist > other.dist; }
};

// Cache "max" move points per unit. We assume the move points a unit has when first observed
// are its full-per-turn movement. At the start of a new turn (movedThisTurn == false), we use
// the cached max for reachability and for budgeting in move().
static std::unordered_map<UnitId, int> g_unitMaxMP;

static inline int getCachedMaxMP(UnitId id, int currentMP) {
    auto it = g_unitMaxMP.find(id);
    if (it == g_unitMaxMP.end()) {
        g_unitMaxMP.emplace(id, currentMP);
        return currentMP;
    }
    // If something ever increases max MP (tech/buff), refresh cache upward.
    if (currentMP > it->second) it->second = currentMP;
    return it->second;
}

static inline int effectiveMPForTurn(const Unit* u, UnitId id) {
    const int cur = u->getMovePoints();
    const int maxMP = getCachedMaxMP(id, cur);
    // If unit has not moved yet this turn, treat it as having full movement.
    return u->movedThisTurn() ? cur : maxMP;
}

int MovementSystem::shortestPathDistance(const Game& game, Pos from, Pos to) {
    if (!game.getMap().inBounds(from) || !game.getMap().inBounds(to)) return -1;
    if (from == to) return 0;

    const int w = game.getMap().getWidth();
    const int h = game.getMap().getHeight();
    const int n = w * h;

    // We keep this function returning an integer "move point" distance.
    // Internally we compute in half-points to support 0.5 road moves.
    const int INF = std::numeric_limits<int>::max() / 4;
    std::vector<int> distHalf(static_cast<size_t>(n), INF);

    auto canStepOn = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;

        // Block occupied tiles (except destination; dest must be empty for move() anyway)
        const auto occ = game.getMap().unitOn(p);
        if (occ != Map::kNoUnit && !(p == to)) return false;

        // Terrain restrictions can go here later (water/ocean/mountain, skills etc.)
        // For now: allow any tile type.
        return true;
    };

    // UnitId is only needed for rule hooks; if you don't have it here, pass Map::kNoUnit.
    // This function is currently used as a generic distance query; we assume no special unit rules.
    const UnitId movingUnit = Map::kNoUnit;

    std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<DijkstraNode>> pq;
    distHalf[static_cast<size_t>(idx(from, w))] = 0;
    pq.push(DijkstraNode{0, from});

    while (!pq.empty()) {
        const DijkstraNode node = pq.top();
        pq.pop();

        const int ci = idx(node.p, w);
        if (node.dist != distHalf[static_cast<size_t>(ci)]) continue;
        if (node.p == to) {
            // Convert half-points to whole move points (no ceil needed; always even).
            return node.dist / 2;
        }

        for (const Pos nb : neighbors8(node.p)) {
            if (!canStepOn(nb)) continue;

            const int step = stepCostHalfPoints(game, movingUnit, node.p, nb);
            const int nd = node.dist + step;
            const int ni = idx(nb, w);

            if (nd < distHalf[static_cast<size_t>(ni)]) {
                distHalf[static_cast<size_t>(ni)] = nd;
                pq.push(DijkstraNode{nd, nb});
            }
        }
    }

    return -1;
}

bool MovementSystem::move(Game& game, UnitId unitId, Pos to) {
    Unit* u = game.getUnit(unitId);
    if (!u) return false;

    const Pos from = u->getPos();
    if (from == to) return false;

    if (!game.getMap().inBounds(from) || !game.getMap().inBounds(to)) return false;

    // Remove: only one move per turn. Now, allow multiple moves per turn until move points are exhausted.

    if (game.getMap().unitOn(to) != Map::kNoUnit) return false;

    // Weighted Dijkstra for this unit with half-point costs.
    const int w = game.getMap().getWidth();
    const int n = w * game.getMap().getHeight();
    const int INF = std::numeric_limits<int>::max() / 4;

    const int mp = effectiveMPForTurn(u, unitId);
    if (mp <= 0) return false;
    // If this is the first move of the turn, ensure the unit's stored MP matches full-per-turn MP.
    if (!u->movedThisTurn() && u->getMovePoints() != mp) {
        u->setMovePoints(mp);
    }
    const int budgetHalf = mp * 2;

    std::vector<int> distHalf(static_cast<size_t>(n), INF);
    std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<DijkstraNode>> pq;

    auto canStepOn = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;
        const auto occ = game.getMap().unitOn(p);
        if (occ != Map::kNoUnit && !(p == to)) return false;
        return true;
    };

    distHalf[static_cast<size_t>(idx(from, w))] = 0;
    pq.push(DijkstraNode{0, from});

    bool found = false;
    int bestTo = INF;

    while (!pq.empty()) {
        const DijkstraNode node = pq.top();
        pq.pop();

        const int ci = idx(node.p, w);
        if (node.dist != distHalf[static_cast<size_t>(ci)]) continue;

        if (node.p == to) {
            found = true;
            bestTo = node.dist;
            break;
        }

        // If current tile forces a stop, don't expand further.
        if (node.p != from && isTerminalAfterEntering(game, node.p, unitId)) {
            continue;
        }

        for (const Pos nb : neighbors8(node.p)) {
            if (!canStepOn(nb)) continue;

            const int step = stepCostHalfPoints(game, unitId, node.p, nb);
            int nd = node.dist + step;

            // Budget rule with "last 0.5 can still enter a 1-cost tile" approximation:
            // If we have exactly 1 half-point left, allow one more step even if it costs 2,
            // but only as a terminal move.
            if (nd > budgetHalf) {
                const int remaining = budgetHalf - node.dist;
                if (!(remaining == 1 && step == 2)) {
                    continue;
                }
                // We allow this one final step by clamping to budgetHalf.
                nd = budgetHalf;
            }

            const int ni = idx(nb, w);
            if (nd < distHalf[static_cast<size_t>(ni)]) {
                distHalf[static_cast<size_t>(ni)] = nd;
                pq.push(DijkstraNode{nd, nb});
            }
        }
    }

    if (!found) return false;

    // Update map occupancy
    game.getMap().setUnitOn(from, Map::kNoUnit);
    game.getMap().setUnitOn(to, unitId);

    // Update unit
    u->setPos(to);
    const int remainingMP = mp - (bestTo / 2);
    u->setMovePoints(remainingMP);
    // Mark that the unit has acted this turn so turn reset restores full move points next round.
    u->setMovedThisTurn(true);

    // Vision update is done in Game::moveUnit via Interaction/Vision systems,
    // but you could also do it here if you want.
    return true;
}

std::vector<Pos> MovementSystem::reachable(const Game& game, UnitId unitId) {
    const Unit* u = game.getUnit(unitId);
    if (!u) return {};

    const Pos start = u->getPos();
    if (!game.getMap().inBounds(start)) return {};

    const int mp = effectiveMPForTurn(u, unitId);
    if (mp <= 0) return {};

    const int w = game.getMap().getWidth();
    const int h = game.getMap().getHeight();
    const int n = w * h;

    const int budgetHalf = mp * 2;
    const int INF = std::numeric_limits<int>::max() / 4;

    std::vector<int> distHalf(static_cast<size_t>(n), INF);
    std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<DijkstraNode>> pq;

    std::vector<Pos> out;
    out.reserve(128);

    auto canStep = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;

        // block occupied tiles (except starting tile)
        const auto occ = game.getMap().unitOn(p);
        if (occ != Map::kNoUnit && !(p == start)) return false;

        // Terrain rules later
        return true;
    };

    distHalf[static_cast<size_t>(idx(start, w))] = 0;
    pq.push(DijkstraNode{0, start});

    while (!pq.empty()) {
        const DijkstraNode node = pq.top();
        pq.pop();

        const int ci = idx(node.p, w);
        if (node.dist != distHalf[static_cast<size_t>(ci)]) continue;

        if (node.p != start) {
            out.push_back(node.p);
        }

        // Terminal tile: you can END here, but you can't move further this turn.
        if (node.p != start && isTerminalAfterEntering(game, node.p, unitId)) {
            continue;
        }

        for (const Pos nb : neighbors8(node.p)) {
            if (!canStep(nb)) continue;

            const int step = stepCostHalfPoints(game, unitId, node.p, nb);
            int nd = node.dist + step;

            if (nd > budgetHalf) {
                const int remaining = budgetHalf - node.dist;
                if (!(remaining == 1 && step == 2)) {
                    continue;
                }
                nd = budgetHalf;
            }

            const int ni = idx(nb, w);
            if (nd < distHalf[static_cast<size_t>(ni)]) {
                distHalf[static_cast<size_t>(ni)] = nd;
                pq.push(DijkstraNode{nd, nb});
            }
        }
    }

    std::sort(out.begin(), out.end(), [](const Pos& a, const Pos& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const Pos& a, const Pos& b) {
        return a.x == b.x && a.y == b.y;
    }), out.end());

    return out;
}