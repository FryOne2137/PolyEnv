//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "Systems/MovementSystem.h"

#include "Game.h" // needs Game::getUnit and Game::getMap()
#include "terrain/BaseTerrainEnum.h"
#include "terrain/BuildingTypeEnum.h"
#include "tech/TechDB.h" // TechId
#include "Systems/VisionSystem.h"
#include "units/UnitFactory.h"

#include <queue>
#include <algorithm>
#include <limits>
#include <array>
#include <iostream>
#include <ostream>



static inline bool isNavalType(UnitType t) {
    switch (t) {
        case UnitType::Raft:
        case UnitType::Scout:
        case UnitType::Rammer:
        case UnitType::Bomber:
        case UnitType::Dinghy:
        case UnitType::Pirate:
        case UnitType::Juggernaut:
            return true;
        default:
            return false;
    }
}

// Water movement is allowed for:
// 1) true naval types, OR
// 2) units that have WaterOnly AND are currently embarked / already on water (prevents accidental WaterOnly on land units).
static inline bool isWaterMover(const Game& game, const Unit* u) {
    if (!u) return false;

    if (isNavalType(u->getType())) return true;

    if (!u->hasSkill(UnitSkill::WaterOnly)) return false;

    // If your Unit supports embark state, use it as the primary guard.
    if (u->isEmbarked()) return true;

    // Fallback safety: allow WaterOnly only when already standing on water/ocean/port.
    const Pos p = u->getPos();
    if (!game.getMap().inBounds(p)) return false;
    const Tile& t = game.getMap().at(p);
    if (t.getBaseTerrain() == BaseTerrainEnum::Water) return true;
    if (t.getBaseTerrain() == BaseTerrainEnum::Ocean) return true;
    if (t.getBuildingType() == BuildingTypeEnum::Port) return true;

    return false;
}


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

// Helper: does this unit have Hide?
static inline bool unitHasHide(const Unit* u) {
    return u && u->hasSkill(UnitSkill::Hide);
}

// Returns true if tile `p` is inside enemy ZoC for `mover`.
// ZoC is projected by enemy units that do NOT have Hide onto their 8-neighborhood.
// Movers with Hide ignore ZoC completely.
static inline bool inEnemyZoC(const Game& game, Pos p, const Unit* mover) {
    if (!mover) return false;
    if (unitHasHide(mover)) return false; // Hide units are not blocked

    const PlayerId moverOwner = mover->getOwnerId();

    for (const Pos nb : neighbors8(p)) {
        if (!game.getMap().inBounds(nb)) continue;
        const UnitId occ = game.getMap().unitOn(nb);
        if (occ == Map::kNoUnit) continue;

        const Unit* other = game.getUnit(occ);
        if (!other) continue;
        if (other->getOwnerId() == moverOwner) continue; // friendly units don't block
        if (unitHasHide(other)) continue;                // Hide units don't project ZoC

        return true;
    }

    return false;
}

// Returns true if p is a land or mountain tile that is adjacent (8-neigh) to water or ocean.
static inline bool isCoastalLandTile(const Game& game, Pos p) {
    if (!game.getMap().inBounds(p)) return false;
    const Tile& t = game.getMap().at(p);
    const bool landish = (t.getBaseTerrain() == BaseTerrainEnum::Land || t.getBaseTerrain() == BaseTerrainEnum::Mountain);
    if (!landish) return false;

    // Coastal = touches Water or Ocean in 8-neighborhood.
    for (const Pos nb : neighbors8(p)) {
        if (!game.getMap().inBounds(nb)) continue;
        const Tile& tn = game.getMap().at(nb);
        if (tn.getBaseTerrain() == BaseTerrainEnum::Water || tn.getBaseTerrain() == BaseTerrainEnum::Ocean) {
            return true;
        }
    }
    return false;
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

static inline bool isTerminalAfterEntering(const Game& game, Pos p, UnitId movingUnit) {
    // WaterOnly / naval movement rule:
    // - Water-capable units may enter land ONLY to disembark, and must STOP immediately.
    //   This prevents walking multiple tiles inland in a single move.
    const Unit* u = game.getUnit(movingUnit);
    if (!u) return false;

    // Only apply to water-capable units (WaterOnly or naval types).
    if (isWaterMover(game, u)) {
        if (!game.getMap().inBounds(p)) return false;
        const Tile& t = game.getMap().at(p);
        const bool landish = (t.getBaseTerrain() == BaseTerrainEnum::Land || t.getBaseTerrain() == BaseTerrainEnum::Mountain);
        if (landish) {
            // Any time a water-capable unit enters land, it must stop.
            return true;
        }
    }

    // Enemy ZoC rule:
    // If you enter a tile that is within 1 of an enemy unit (that doesn't have Hide), you must STOP.
    // Hide units are not blocked and do not stop.
    if (inEnemyZoC(game, p, u)) {
        return true;
    }

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

// Fog-of-war: helper to test if a tile is currently visible/revealed to a player.
// VisibilityEnum is treated as a bitmask where bit (1 << playerId) marks visibility.
static inline bool tileVisibleToPlayer(const Tile& t, PlayerId pid) {
    const uint16_t vis  = static_cast<uint16_t>(t.getVisibility());
    const uint16_t mask = static_cast<uint16_t>(1u) << static_cast<uint16_t>(pid);
    return (vis & mask) != 0;
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

        const Tile& t = game.getMap().at(p);

        // Generic distance query (no unit/owner context here):
        // do NOT allow paths that go through Water/Ocean, because those require
        // Fishing/Port/ownership checks handled by move()/reachable().
        if (t.getBaseTerrain() == BaseTerrainEnum::Water) return false;
        if (t.getBaseTerrain() == BaseTerrainEnum::Ocean) return false;

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

    // Default rule: you cannot move after you already moved.
    // Escape rule: units with Escape may move after attacking.
    if (u->movedThisTurn()) return false;
    if (u->attackedThisTurn() && !u->hasSkill(UnitSkill::Escape)) return false;

    // Remove: only one move per turn. Now, allow multiple moves per turn until move points are exhausted.

    if (game.getMap().unitOn(to) != Map::kNoUnit) return false;

    // Destination restrictions.
    {
        const Tile& tt = game.getMap().at(to);
        const Tile& tf = game.getMap().at(from);
        const PlayerId mover = u->getOwnerId();

        // Fog-of-war: you may only END movement on a tile that is visible/revealed for you.
        if (!tileVisibleToPlayer(tt, mover)) return false;

        const bool hasClimbing = game.getPlayer(mover).hasTech(TechId::Climbing);
        const bool hasFishing  = game.getPlayer(mover).hasTech(TechId::Fishing);
        const bool hasSailing  = game.getPlayer(mover).hasTech(TechId::Sailing);

        // Mountains require Climbing
        if (tt.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool toIsWater = (tt.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool toIsOcean = (tt.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool toIsPort  = (tt.getBuildingType() == BuildingTypeEnum::Port);

        const bool fromIsLandish = (tf.getBaseTerrain() == BaseTerrainEnum::Land ||
                                   tf.getBaseTerrain() == BaseTerrainEnum::Mountain);

        const bool unitWaterCapable = isWaterMover(game, u);

        // --- Rules for LAND units (not water capable) ---
        // From land -> Water/Ocean: ONLY onto a PORT, requires Fishing, and PORT must be inside mover-owned city territory.
        if (!unitWaterCapable && fromIsLandish && (toIsWater || toIsOcean)) {
            if (!toIsPort) return false;
            if (!hasFishing) return false;

            const CityId tid = tt.getTerritoryCityId();
            if (tid == kNoCity) return false;
            const City* tc = game.getCity(tid);
            if (!tc) return false;
            if (static_cast<PlayerId>(tc->getOwnerId()) != mover) return false;

            // Ocean travel requires Sailing (even when entering via an ocean port).
            if (toIsOcean && !hasSailing) return false;
        }

        // Land unit cannot step onto Water/Ocean otherwise.
        if (!unitWaterCapable && (toIsWater || toIsOcean) && !toIsPort) {
            return false;
        }

        // --- Rules for WATER-capable units (WaterOnly / naval) ---
        // Ocean requires Sailing.
        if (unitWaterCapable && toIsOcean && !hasSailing) return false;

        // Water-capable units may step onto land ONLY if it is coastal (touches water/ocean).
        // This prevents walking multiple tiles inland.
        const bool toIsLandish = (tt.getBaseTerrain() == BaseTerrainEnum::Land || tt.getBaseTerrain() == BaseTerrainEnum::Mountain);
        if (unitWaterCapable && toIsLandish) {
            if (!isCoastalLandTile(game, to)) return false;
        }

        // PORT validation: entering a Port tile always requires Fishing and that the Port is on mover-owned city territory.
        // (applies to both land and water-capable units)
        if (toIsPort) {
            if (!hasFishing) return false;

            const CityId tid = tt.getTerritoryCityId();
            if (tid == kNoCity) return false;
            const City* tc = game.getCity(tid);
            if (!tc) return false;
            if (static_cast<PlayerId>(tc->getOwnerId()) != mover) return false;

            if (toIsOcean && !hasSailing) return false;
        }
    }

    // Weighted Dijkstra for this unit with half-point costs.
    const int w = game.getMap().getWidth();
    const int n = w * game.getMap().getHeight();
    const int INF = std::numeric_limits<int>::max() / 4;

    const int mp = u->getMovePoints();
    if (mp <= 0) return false;
    const int budgetHalf = mp * 2;

    const PlayerId moverOwner = u->getOwnerId();
    const bool hasClimbing = game.getPlayer(moverOwner).hasTech(TechId::Climbing);

    auto canStepOn = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;

        // Occupied tile logic:
        const UnitId occ = game.getMap().unitOn(p);
        if (occ != Map::kNoUnit) {
            // Destination must be empty (checked earlier), but keep this defensive.
            if (p == to) return false;

            // Allow passing through FRIENDLY units; block ENEMY units.
            const Unit* other = game.getUnit(occ);
            if (other && other->getOwnerId() != moverOwner) {
                return false;
            }
        }

        const Tile& t = game.getMap().at(p);

        // Mountain restriction: cannot enter mountains without Climbing.
        if (t.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool pIsWater = (t.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool pIsOcean = (t.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool pIsPort  = (t.getBuildingType() == BuildingTypeEnum::Port);
        const bool pIsLandish = (t.getBaseTerrain() == BaseTerrainEnum::Land || t.getBaseTerrain() == BaseTerrainEnum::Mountain);

        const bool hasFishing = game.getPlayer(moverOwner).hasTech(TechId::Fishing);
        const bool hasSailing = game.getPlayer(moverOwner).hasTech(TechId::Sailing);
        const bool unitWaterCapable = isWaterMover(game, u);

        // Water-capable units: Water is OK, Ocean only with Sailing.
        if (unitWaterCapable) {
            if (pIsOcean && !hasSailing) return false;
            if (pIsLandish) {
                // Only allow stepping onto coastal land to avoid moving inland.
                if (!isCoastalLandTile(game, p)) return false;
            }
        } else {
            // Land units: cannot traverse Water/Ocean unless it's a valid owned Port with Fishing.
            if ((pIsWater || pIsOcean) && !pIsPort) return false;
        }

        // Port tiles: always require Fishing and must be inside mover-owned city territory.
        if (pIsPort) {
            if (!hasFishing) return false;

            const CityId tid = t.getTerritoryCityId();
            if (tid == kNoCity) return false;
            const City* tc = game.getCity(tid);
            if (!tc) return false;
            if (static_cast<PlayerId>(tc->getOwnerId()) != moverOwner) return false;

            if (pIsOcean && !hasSailing) return false;
        }

        return true;
    };

    std::vector<int> distHalf(static_cast<size_t>(n), INF);
    std::priority_queue<DijkstraNode, std::vector<DijkstraNode>, std::greater<DijkstraNode>> pq;
    std::vector<int> prev(static_cast<size_t>(n), -1);

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
                prev[static_cast<size_t>(ni)] = ci;
                pq.push(DijkstraNode{nd, nb});
            }
        }
    }

    if (!found) return false;

    // Reconstruct path (inclusive: from -> ... -> to)
    std::vector<Pos> path;
    path.reserve(64);

    auto buildFromPrev = [&]() -> bool {
        const int startI = idx(from, w);
        int curI = idx(to, w);

        if (curI != startI && prev[static_cast<size_t>(curI)] == -1) {
            return false; // no chain
        }

        std::vector<Pos> rev;
        rev.reserve(64);

        int safety = 0;
        while (curI != -1 && safety++ < n) {
            rev.push_back(fromIdx(curI, w));
            if (curI == startI) break;
            curI = prev[static_cast<size_t>(curI)];
        }

        if (rev.empty() || rev.back().x != from.x || rev.back().y != from.y) {
            return false; // chain did not reach start
        }

        std::reverse(rev.begin(), rev.end());
        path = std::move(rev);
        return true;
    };

    // Fallback: reconstruct by distHalf gradient (works with roads/costs)
    auto buildFromDist = [&]() -> bool {
        const int startI = idx(from, w);
        int curI = idx(to, w);

        if (distHalf[static_cast<size_t>(curI)] >= INF) return false;

        std::vector<Pos> rev;
        rev.reserve(64);

        int safety = 0;
        while (safety++ < n) {
            rev.push_back(fromIdx(curI, w));
            if (curI == startI) break;

            const Pos curP = fromIdx(curI, w);
            const int curD = distHalf[static_cast<size_t>(curI)];

            int bestPrev = -1;
            int bestPrevDist = INF;

            for (const Pos nb : neighbors8(curP)) {
                if (!game.getMap().inBounds(nb)) continue;

                // allow stepping onto start even though it's occupied by this unit (map not updated yet)
                const auto occ = game.getMap().unitOn(nb);
                if (occ != Map::kNoUnit && !(nb == from) && !(nb == to)) continue;

                // keep same terrain restrictions as in forward search
                const Tile& tnb = game.getMap().at(nb);
                if (tnb.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) continue;

                // If the predecessor tile would have been terminal, we should not be reconstructing
                // a path that continues beyond it. This matters especially for WaterOnly units that
                // can step onto coastal land only as a terminal end.
                if (nb != from && isTerminalAfterEntering(game, curP, unitId)) {
                    // We are currently at curP and trying to come from nb. If curP is terminal, it can be
                    // the final tile but we shouldn't have arrived here from another land tile and then
                    // continued; the forward search already prevented expansions from terminal tiles.
                    // Keep reconstruction conservative by allowing only if curP is the destination.
                    // (We are reconstructing from destination backwards, so allow this step.)
                }

                const int ni = idx(nb, w);
                const int nbD = distHalf[static_cast<size_t>(ni)];
                if (nbD >= INF) continue;

                const int step = stepCostHalfPoints(game, unitId, nb, curP);

                // normal case
                bool ok = (nbD + step == curD);

                // handle the “clamp to budgetHalf” last-step rule:
                // if cur is at budgetHalf, allow predecessor that overshoots but was clamped
                if (!ok && curD == budgetHalf && nbD < curD && nbD + step > curD) {
                    ok = true;
                }

                if (!ok) continue;

                if (nbD < bestPrevDist) {
                    bestPrevDist = nbD;
                    bestPrev = ni;
                }
            }

            if (bestPrev == -1) return false;
            curI = bestPrev;
        }

        if (rev.empty() || !(rev.back() == from)) return false;
        std::reverse(rev.begin(), rev.end());
        path = std::move(rev);
        return true;
    };

    if (!buildFromPrev()) {
        // only if prev-chain fails
        if (!buildFromDist()) {
            // hard fallback (should be rare)
            path.clear();
            path.push_back(from);
            path.push_back(to);
        }
    }
    // Update map occupancy
    game.getMap().setUnitOn(from, Map::kNoUnit);
    game.getMap().setUnitOn(to, unitId);

    // Update unit
    u->setPos(to);
    // One move per turn: movement budget is based on base movePoints, but we don't carry leftover MP.
    u->setMovedThisTurn(true);

    // ---- Port embark / disembark ----
    // Rule:
    // - When a LAND unit steps onto a PORT tile from a LAND tile, it becomes a Raft (or Juggernaut if Giant).
    // - When an embarked unit steps onto a LAND tile, it reverts to its stored original land type.
    {
        Tile& dst = game.getMap().at(to);
        Tile& src = game.getMap().at(from);

        const bool dstIsPort = (dst.getBuildingType() == BuildingTypeEnum::Port);
        const bool srcIsLandish = (src.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  src.getBaseTerrain() == BaseTerrainEnum::Mountain);
        const bool dstIsLandish = (dst.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  dst.getBaseTerrain() == BaseTerrainEnum::Mountain);

        // Defensive: only the owning player with Fishing can embark via a Port.
        const PlayerId mover = u->getOwnerId();
        const bool hasFishing = game.getPlayer(mover).hasTech(TechId::Fishing);
        bool portOwnedByMover = false;
        if (dstIsPort) {
            const CityId tid = dst.getTerritoryCityId();
            if (tid != kNoCity) {
                if (const City* tc = game.getCity(tid)) {
                    portOwnedByMover = (static_cast<PlayerId>(tc->getOwnerId()) == mover);
                }
            }
        }

#ifndef NDEBUG
        // Helpful debug to verify why embark does/doesn't trigger.
        if (dstIsPort) {
            std::cout << "[Move] stepped onto PORT at (" << to.x << "," << to.y << ")"
                      << " srcLand=" << (srcIsLandish ? 1 : 0)
                      << " uType=" << int(u->getType())
                      << " waterCapable=" << (isWaterMover(game, u) ? 1 : 0)
                      << " embarked=" << (u->isEmbarked() ? 1 : 0)
                      << " hasFishing=" << (hasFishing ? 1 : 0)
                      << " owned=" << (portOwnedByMover ? 1 : 0)
                      << "\n";
        }
#endif

        // Embark: land unit -> Raft/Juggernaut when stepping onto a Port from land.
        // Do NOT use isWaterCapable() here as the only gate, because some future unit types
        // may be water-capable without being an actual embarked carrier.
        const bool isAlreadyNavalType = isNavalType(u->getType());

        if (dstIsPort && srcIsLandish && !isAlreadyNavalType && !u->isEmbarked()) {
            // If you somehow arrived on a port without satisfying the rules, do not convert.
            if (hasFishing && portOwnedByMover) {
                const UnitType original = u->getType();
                const UnitType naval = (original == UnitType::Giant) ? UnitType::Juggernaut : UnitType::Raft;

                Unit nu = UnitFactory::create(naval, u->getOwnerId(), to);
                nu.setId(u->getId());

                // Preserve HP and MaxHP from the land unit (Raft must inherit HP pool).
                const int oldMaxHp = u->getMaxHealth();
                const int oldHp    = u->getHealth();
                nu.setMaxHealth(oldMaxHp);
                nu.setHealth(std::min(oldHp, oldMaxHp));

                // Keep important runtime state.
                nu.setVeteran(u->isVeteran());
                nu.setPoisoned(u->poisoned());
                nu.setAttackedThisTurn(u->attackedThisTurn());

                // Remember original unit type, mark as embarked carrier.
                nu.setEmbarkedBaseType(original);
                nu.addSkill(UnitSkill::WaterOnly);

                // IMPORTANT: embarking ends the unit's movement/action for this turn.
                nu.setMovedThisTurn(true);
                nu.setAttackedThisTurn(true);

                *u = nu;

#ifndef NDEBUG
                std::cout << "[Move] EMBARK: " << int(original) << " -> " << int(naval) << "\n";
#endif
            }
        }

        // Disembark: embarked carrier stepping onto land -> revert to original land unit type.
        if (dstIsLandish && u->isEmbarked()) {
            const UnitType original = u->getEmbarkedBaseType();
            if (original != UnitType::Unknown) {
                Unit nu = UnitFactory::create(original, u->getOwnerId(), to);
                nu.setId(u->getId());

                // Keep HP / status.
                nu.setHealth(u->getHealth());
                nu.setVeteran(u->isVeteran());
                nu.setPoisoned(u->poisoned());
                nu.setAttackedThisTurn(u->attackedThisTurn());

                nu.clearEmbarkedBaseType();
                nu.removeSkill(UnitSkill::WaterOnly);

                // IMPORTANT: disembarking ends movement/action for this turn.
                nu.setMovedThisTurn(true);
                nu.setAttackedThisTurn(true);

                *u = nu;

#ifndef NDEBUG
                std::cout << "[Move] DISEMBARK: -> " << int(original) << "\n";
#endif
            }
        }
    }

    // Fog-of-war: reveal tiles the unit passed through (whole path) using the unit's vision range.
    // If the unit ends on a Mountain tile, ensure at least radius 2 from the destination.
    {
        const PlayerId pid = u->getOwnerId();
        const int baseVision = u->getVisionRange();

        // Reveal along the whole traversed path.
        for (const Pos& pp : path) {
            VisionSystem::revealArea(game, pid, pp, baseVision);
        }

        // If the unit ends on a mountain, grant the mountain vision bonus at the final position.
        const Tile& endTile = game.getMap().at(to);
        if (endTile.getBaseTerrain() == BaseTerrainEnum::Mountain) {
            VisionSystem::revealArea(game, pid, to, std::max(2, baseVision));
        }
    }

    return true;
}

std::vector<Pos> MovementSystem::reachable(const Game& game, UnitId unitId) {
    const Unit* u = game.getUnit(unitId);
    if (!u) return {};

    // Default rule: no movement after the unit has moved.
    // Escape rule: units with Escape may move after attacking.
    if (u->movedThisTurn()) return {};
    if (u->attackedThisTurn() && !u->hasSkill(UnitSkill::Escape)) return {};

    const Pos start = u->getPos();
    if (!game.getMap().inBounds(start)) return {};

    const int mp = u->getMovePoints();
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

    const PlayerId moverOwner = u->getOwnerId();
    const bool hasClimbing = game.getPlayer(moverOwner).hasTech(TechId::Climbing);

    auto canStep = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;

        // block occupied tiles (except starting tile)
        const UnitId occ = game.getMap().unitOn(p);
        if (occ != Map::kNoUnit) {
            // Can't end on an occupied tile, but allow passing through FRIENDLY units.
            // (start is occupied by this unit)
            if (!(p == start)) {
                const Unit* other = game.getUnit(occ);
                if (other && other->getOwnerId() != moverOwner) {
                    return false;
                }
            }
        }

        const Tile& t = game.getMap().at(p);

        // Mountain restriction: cannot enter mountains without Climbing.
        if (t.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool toIsWater = (t.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool toIsOcean = (t.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool toIsPort  = (t.getBuildingType() == BuildingTypeEnum::Port);
        const bool toIsLandish = (t.getBaseTerrain() == BaseTerrainEnum::Land || t.getBaseTerrain() == BaseTerrainEnum::Mountain);

        const bool hasFishing = game.getPlayer(moverOwner).hasTech(TechId::Fishing);
        const bool hasSailing = game.getPlayer(moverOwner).hasTech(TechId::Sailing);
        const bool unitWaterCapable = isWaterMover(game, u);

        if (unitWaterCapable) {
            // WaterOnly/naval: Water ok, Ocean only with Sailing.
            if (toIsOcean && !hasSailing) return false;

            // Land only if coastal.
            if (toIsLandish) {
                if (!isCoastalLandTile(game, p)) return false;
            }
        } else {
            // Land units: cannot step onto Water/Ocean unless it's a valid owned Port.
            if ((toIsWater || toIsOcean) && !toIsPort) return false;
        }

        // Port entry rule: always requires Fishing and owned territory (+ Sailing if ocean).
        if (toIsPort) {
            if (!hasFishing) return false;

            const CityId tid = t.getTerritoryCityId();
            if (tid == kNoCity) return false;
            const City* tc = game.getCity(tid);
            if (!tc) return false;
            if (static_cast<PlayerId>(tc->getOwnerId()) != moverOwner) return false;

            if (toIsOcean && !hasSailing) return false;
        }

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
            const Tile& tn = game.getMap().at(node.p);
            if (tileVisibleToPlayer(tn, moverOwner)) {
                out.push_back(node.p);
            }
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