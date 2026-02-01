//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "Systems/MovementSystem.h"

#include "Game.h" // needs Game::getUnit and Game::getMap()
#include "terrain/BaseTerrainEnum.h"
#include "terrain/BuildingTypeEnum.h"
#include "terrain/RoadBridgeEnum.h"
#include "terrain/ResourcesEnum.h"
#include "tech/TechDB.h" // TechId
#include "Systems/VisionSystem.h"
#include "units/UnitFactory.h"

#include <queue>
#include <vector>
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

static inline bool isRoadLikeTile(const Game& game, Pos p, UnitId movingUnit) {
    if (!game.getMap().inBounds(p)) return false;

    // Some skills (e.g., Fly/Creep) may not use roads. If you add them later,
    // gate them here.
    if (movingUnit != Map::kNoUnit) {
        const Unit* u = game.getUnit(movingUnit);
        if (!u) return false;
        // Example hooks (uncomment if you have these skills):
        // if (u->hasSkill(UnitSkill::Fly)) return false;
        // if (u->hasSkill(UnitSkill::Creep)) return false;
    }

    const Tile& t = game.getMap().at(p);

    // Roads can't exist on mountains in Polytopia; treat mountain as non-roadlike defensively.
    if (t.getBaseTerrain() == BaseTerrainEnum::Mountain) return false;

    const RoadBridgeEnum rb = t.getRoadBridge();

    // Compatibility + robustness:
    // In Polytopia, city/village tiles behave like roads. Even if you already place roads under them,
    // this makes older saves/maps (before you added auto-road under settlements) still work.
    const SettlementTypeEnum st = t.getSettlementType();
    const bool settlementRoad = (st == SettlementTypeEnum::City) || (st == SettlementTypeEnum::Village);

    return settlementRoad || (rb == RoadBridgeEnum::Road) || (rb == RoadBridgeEnum::Bridge);
}

static inline bool isEnemyTerritoryForRoadBonus(const Game& game, Pos p, UnitId movingUnit) {
    if (movingUnit == Map::kNoUnit) return false; // generic queries
    const Unit* u = game.getUnit(movingUnit);
    if (!u) return true; // be conservative

    if (!game.getMap().inBounds(p)) return true;

    const Tile& t = game.getMap().at(p);
    const CityId cid = t.getTerritoryCityId();

    // Neutral (no city) => treat as allowed for road bonus.
    if (cid == kNoCity) return false;

    const City* c = game.getCity(cid);
    if (!c) return true;

    return static_cast<PlayerId>(c->getOwnerId()) != u->getOwnerId();
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
    // Road/bridge move cost is 0.5 (= 1 half-point) when both tiles are road-like AND the move
    // is entirely within friendly or neutral territory (no bonus on enemy territory roads).
    const bool fromRoad = isRoadLikeTile(game, from, movingUnit);
    const bool toRoad   = isRoadLikeTile(game, to, movingUnit);

    if (!fromRoad || !toRoad) return 2;

    const bool fromEnemy = isEnemyTerritoryForRoadBonus(game, from, movingUnit);
    const bool toEnemy   = isEnemyTerritoryForRoadBonus(game, to, movingUnit);

    const bool roadOK = !fromEnemy && !toEnemy;
    return roadOK ? 1 : 2;
}

struct DijkstraNode {
    int dist;
    Pos p;
    bool operator>(const DijkstraNode& other) const { return dist > other.dist; }
};

namespace {
// Thread-local scratch to avoid allocations and enable very fast bucket-Dijkstra (Dial)
// when the maximum distance (budget) is small (true for per-turn unit movement).
struct BucketScratch {
    std::vector<int> distHalf;   // valid when stamp[i] == epoch
    std::vector<int> prev;       // predecessor index for path reconstruction (when used)
    std::vector<int> stamp;      // epoch stamp for distHalf

    std::vector<int> next;       // linked-list next pointers for bucket queues
    std::vector<int> head;       // bucket heads (size = maxD+1)

    std::vector<int> outStamp;   // epoch stamp for reachable output de-dup

    int epoch = 1;
    int outEpoch = 1;

    void ensure(int n) {
        if (static_cast<int>(distHalf.size()) < n) {
            distHalf.resize(n);
            prev.resize(n);
            stamp.resize(n, 0);
            next.resize(n);
            outStamp.resize(n, 0);
        }
    }

    void bumpEpoch() {
        if (++epoch == std::numeric_limits<int>::max()) {
            std::fill(stamp.begin(), stamp.end(), 0);
            epoch = 1;
        }
    }

    void bumpOutEpoch() {
        if (++outEpoch == std::numeric_limits<int>::max()) {
            std::fill(outStamp.begin(), outStamp.end(), 0);
            outEpoch = 1;
        }
    }

    inline int getDist(int i, int INF) const {
        return (stamp[static_cast<size_t>(i)] == epoch) ? distHalf[static_cast<size_t>(i)] : INF;
    }

    inline void setDist(int i, int d) {
        stamp[static_cast<size_t>(i)] = epoch;
        distHalf[static_cast<size_t>(i)] = d;
    }

    inline void resetBuckets(int maxD) {
        head.assign(static_cast<size_t>(maxD + 1), -1);
    }

    inline void pushBucket(int d, int node) {
        next[static_cast<size_t>(node)] = head[static_cast<size_t>(d)];
        head[static_cast<size_t>(d)] = node;
    }

    inline int popBucket(int d) {
        const int node = head[static_cast<size_t>(d)];
        head[static_cast<size_t>(d)] = next[static_cast<size_t>(node)];
        return node;
    }
};

static thread_local BucketScratch g_bucket;

} // namespace

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
        const bool toIsBridge = (tt.getRoadBridge() == RoadBridgeEnum::Bridge);

        const bool fromIsLandish = (tf.getBaseTerrain() == BaseTerrainEnum::Land ||
                                   tf.getBaseTerrain() == BaseTerrainEnum::Mountain);

        const bool unitWaterCapable = isWaterMover(game, u);

        // --- Rules for LAND units (not water capable) ---
        // From land -> Water/Ocean:
        // - PORT: requires Fishing and PORT must be inside mover-owned city territory (Ocean also requires Sailing).
        // - BRIDGE on Water: allows land units to cross one water tile (Polytopia-style), no Fishing required.
        if (!unitWaterCapable && fromIsLandish && (toIsWater || toIsOcean)) {
            if (toIsBridge && toIsWater) {
                // Bridge crossing is allowed without port/fishing/ownership rules.
            } else {
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
        }

        // Land unit cannot step onto Water/Ocean otherwise (Bridge on Water is allowed).
        if (!unitWaterCapable && (toIsWater || toIsOcean) && !(toIsPort || (toIsBridge && toIsWater))) {
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
    // Optimized: bucket-Dijkstra (Dial) because distances are small (<= mp*2 half-points) and edge weights are {1,2}.
    auto& map = game.getMap();
    const int w = map.getWidth();
    const int h = map.getHeight();
    const int n = w * h;
    const int INF = std::numeric_limits<int>::max() / 4;

    const int mp = u->getMovePoints();
    if (mp <= 0) return false;
    const int budgetHalf = mp * 2;

    const PlayerId moverOwner = u->getOwnerId();

    // Precompute tech flags once.
    const bool hasClimbing = game.getPlayer(moverOwner).hasTech(TechId::Climbing);
    const bool hasFishing  = game.getPlayer(moverOwner).hasTech(TechId::Fishing);
    const bool hasSailing  = game.getPlayer(moverOwner).hasTech(TechId::Sailing);

    const bool unitWaterCapable = isWaterMover(game, u);

    auto canStepOn = [&](Pos p) -> bool {
        if (!map.inBounds(p)) return false;

        // Occupied tile logic:
        const UnitId occ = map.unitOn(p);
        if (occ != Map::kNoUnit) {
            // Destination must be empty (checked earlier), but keep this defensive.
            if (p == to) return false;

            // Allow passing through FRIENDLY units; block ENEMY units.
            const Unit* other = game.getUnit(occ);
            if (other && other->getOwnerId() != moverOwner) {
                return false;
            }
        }

        const Tile& t = map.at(p);

        // Mountain restriction: cannot enter mountains without Climbing.
        if (t.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool pIsWater   = (t.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool pIsOcean   = (t.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool pIsPort    = (t.getBuildingType() == BuildingTypeEnum::Port);
        const bool pIsLandish = (t.getBaseTerrain() == BaseTerrainEnum::Land || t.getBaseTerrain() == BaseTerrainEnum::Mountain);
        const bool pIsBridge  = (t.getRoadBridge() == RoadBridgeEnum::Bridge);

        // Water-capable units: Water is OK, Ocean only with Sailing.
        if (unitWaterCapable) {
            if (pIsOcean && !hasSailing) return false;
            if (pIsLandish) {
                // Only allow stepping onto coastal land to avoid moving inland.
                if (!isCoastalLandTile(game, p)) return false;
            }
        } else {
            // Land units: cannot traverse Water/Ocean unless it's a valid owned Port with Fishing,
            // OR a Bridge on Water (Polytopia-style).
            if ((pIsWater || pIsOcean) && !(pIsPort || (pIsBridge && pIsWater))) return false;
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

    // --- Bucket Dijkstra ---
    g_bucket.ensure(n);
    g_bucket.bumpEpoch();
    g_bucket.resetBuckets(budgetHalf);

    const int startI = idx(from, w);
    const int goalI  = idx(to, w);

    g_bucket.setDist(startI, 0);
    g_bucket.prev[static_cast<size_t>(startI)] = -1;
    g_bucket.pushBucket(0, startI);

    bool found = false;

    for (int d = 0; d <= budgetHalf; ++d) {
        while (g_bucket.head[static_cast<size_t>(d)] != -1) {
            const int ci = g_bucket.popBucket(d);

            // Skip stale entries.
            if (g_bucket.getDist(ci, INF) != d) continue;

            if (ci == goalI) {
                found = true;
                break;
            }

            const Pos curP = fromIdx(ci, w);

            // If current tile forces a stop, don't expand further.
            if (curP != from && isTerminalAfterEntering(game, curP, unitId)) {
                continue;
            }

            for (const Pos nb : neighbors8(curP)) {
                if (!canStepOn(nb)) continue;

                const int step = stepCostHalfPoints(game, unitId, curP, nb);
                int nd = d + step;

                // Budget rule with "last 0.5 can still enter a 1-cost tile" approximation:
                if (nd > budgetHalf) {
                    const int remaining = budgetHalf - d;
                    if (!(remaining == 1 && step == 2)) {
                        continue;
                    }
                    nd = budgetHalf;
                }

                const int ni = idx(nb, w);
                const int old = g_bucket.getDist(ni, INF);
                if (nd < old) {
                    g_bucket.setDist(ni, nd);
                    g_bucket.prev[static_cast<size_t>(ni)] = ci;
                    g_bucket.pushBucket(nd, ni);
                }
            }
        }

        if (found) break;
    }

    if (!found) return false;

    // Reconstruct path (inclusive: from -> ... -> to) using the predecessor chain.
    std::vector<Pos> path;
    path.reserve(64);

    {
        const int startI = idx(from, w);
        const int goalI  = idx(to, w);
        int curI = goalI;

        // If we somehow lack a chain (should be rare), fall back to a direct segment.
        if (curI != startI && g_bucket.prev[static_cast<size_t>(curI)] == -1) {
            path.push_back(from);
            path.push_back(to);
        } else {
            std::vector<Pos> rev;
            rev.reserve(64);

            int safety = 0;
            while (curI != -1 && safety++ < n) {
                rev.push_back(fromIdx(curI, w));
                if (curI == startI) break;
                curI = g_bucket.prev[static_cast<size_t>(curI)];
            }

            if (rev.empty() || !(rev.back() == from)) {
                path.push_back(from);
                path.push_back(to);
            } else {
                std::reverse(rev.begin(), rev.end());
                path = std::move(rev);
            }
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

    // Removed unused distHalf and pq from previous implementation.

    std::vector<Pos> out;
    out.reserve(128);

    const PlayerId moverOwner = u->getOwnerId();
    const bool hasClimbing = game.getPlayer(moverOwner).hasTech(TechId::Climbing);
    const bool hasFishing  = game.getPlayer(moverOwner).hasTech(TechId::Fishing);
    const bool hasSailing  = game.getPlayer(moverOwner).hasTech(TechId::Sailing);

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

        const bool toIsWater   = (t.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool toIsOcean   = (t.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool toIsPort    = (t.getBuildingType() == BuildingTypeEnum::Port);
        const bool toIsLandish = (t.getBaseTerrain() == BaseTerrainEnum::Land || t.getBaseTerrain() == BaseTerrainEnum::Mountain);
        const bool toIsBridge  = (t.getRoadBridge() == RoadBridgeEnum::Bridge);

        const bool unitWaterCapable = isWaterMover(game, u);

        if (unitWaterCapable) {
            // WaterOnly/naval: Water ok, Ocean only with Sailing.
            if (toIsOcean && !hasSailing) return false;

            // Land only if coastal.
            if (toIsLandish) {
                if (!isCoastalLandTile(game, p)) return false;
            }
        } else {
            // Land units: cannot step onto Water/Ocean unless it's a valid owned Port,
            // OR a Bridge on Water (Polytopia-style).
            if ((toIsWater || toIsOcean) && !(toIsPort || (toIsBridge && toIsWater))) return false;
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

    // Bucket-Dijkstra (Dial) for reachable: extremely fast for small budgets and weights {1,2}.
    g_bucket.ensure(n);
    g_bucket.bumpEpoch();
    g_bucket.bumpOutEpoch();
    g_bucket.resetBuckets(budgetHalf);

    const int startI = idx(start, w);
    g_bucket.setDist(startI, 0);
    g_bucket.pushBucket(0, startI);

    for (int d = 0; d <= budgetHalf; ++d) {
        while (g_bucket.head[static_cast<size_t>(d)] != -1) {
            const int ci = g_bucket.popBucket(d);
            if (g_bucket.getDist(ci, INF) != d) continue; // stale

            const Pos curP = fromIdx(ci, w);

            if (curP != start) {
                const Tile& tn = game.getMap().at(curP);
                if (tileVisibleToPlayer(tn, moverOwner)) {
                    // Dedup without sorting.
                    if (g_bucket.outStamp[static_cast<size_t>(ci)] != g_bucket.outEpoch) {
                        g_bucket.outStamp[static_cast<size_t>(ci)] = g_bucket.outEpoch;
                        out.push_back(curP);
                    }
                }
            }

            // Terminal tile: you can END here, but you can't move further this turn.
            if (curP != start && isTerminalAfterEntering(game, curP, unitId)) {
                continue;
            }

            for (const Pos nb : neighbors8(curP)) {
                if (!canStep(nb)) continue;

                const int step = stepCostHalfPoints(game, unitId, curP, nb);
                int nd = d + step;

                if (nd > budgetHalf) {
                    const int remaining = budgetHalf - d;
                    if (!(remaining == 1 && step == 2)) {
                        continue;
                    }
                    nd = budgetHalf;
                }

                const int ni = idx(nb, w);
                const int old = g_bucket.getDist(ni, INF);
                if (nd < old) {
                    g_bucket.setDist(ni, nd);
                    g_bucket.pushBucket(nd, ni);
                }
            }
        }
    }

    return out;
}