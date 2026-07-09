

// Forced push used by ruin/super-unit spawns. Returns true if unit survived.

//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "systems/MovementSystem.h"

#include "../game/Game.h"
#include "UnitSystem.h"
#include "systems/PlayerSystem.h"
#include "terrain/BaseTerrainEnum.h"
#include "terrain/BuildingTypeEnum.h"
#include "terrain/RoadBridgeEnum.h"
#include "terrain/ResourcesEnum.h"
#include "../content/tech/TechDB.h"
#include "systems/VisionSystem.h"
#include "systems/CitySystem.h"
#include "systems/InfiltrationSystem.h"
#include "systems/CombatSystem.h"
#include "../content/units/UnitFactory.h"

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
static inline bool isWaterMover(const Game& game, UnitId uid) {
    if (uid == Map::kNoUnit) return false;
    if (!UnitSystem::unitExists(game, uid)) return false;

    const UnitType t = UnitSystem::getType(game, uid);
    if (isNavalType(t)) return true;

    if (!UnitSystem::hasSkill(game, uid, UnitSkill::WaterOnly)) return false;

    // If your Unit supports embark state, use it as the primary guard.
    if (UnitSystem::isEmbarked(game, uid)) return true;

    // Fallback safety: allow WaterOnly only when already standing on water/ocean/port.
    const Pos p = UnitSystem::getPos(game, uid);
    if (!game.getMap().inBounds(p)) return false;
    const Tile& tile = game.getMap().at(p);
    if (tile.getBaseTerrain() == BaseTerrainEnum::Water) return true;
    if (tile.getBaseTerrain() == BaseTerrainEnum::Ocean) return true;
    if (tile.getBuildingType() == BuildingTypeEnum::Port) return true;

    return false;
}


static inline int idx(Pos p, int w) {
    return p.y * w + p.x;
}

static inline Pos fromIdx(int i, int w) {
    return Pos{ i % w, i / w };
}

// Fast neighbor deltas for 8-neighborhood.
static constexpr int kNbDx8[8] = {  1, -1,  0,  0,  1,  1, -1, -1 };
static constexpr int kNbDy8[8] = {  0,  0,  1, -1,  1, -1,  1, -1 };

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
static inline bool unitHasHide(const Game& game, UnitId uid) {
    return (uid != Map::kNoUnit) && UnitSystem::unitExists(game, uid) && UnitSystem::hasSkill(game, uid, UnitSkill::Hide);
}

static inline bool unitHasCreep(const Game& game, UnitId uid) {
    return (uid != Map::kNoUnit) && UnitSystem::unitExists(game, uid) && UnitSystem::hasSkill(game, uid, UnitSkill::Creep);
}

// Hide becomes "active stealth" after the unit performs at least one move.
// We use lastMoveDir != (0,0) as the marker.
static inline bool unitHasActivatedHide(const Game& game, UnitId uid) {
    if (!unitHasHide(game, uid)) return false;
    const Pos d = UnitSystem::getLastMoveDir(game, uid);
    return !(d.x == 0 && d.y == 0);
}

// Returns true if tile `p` is inside enemy ZoC for `moverUid`.
// ZoC is projected by enemy units that do NOT have Hide onto their 8-neighborhood.
// Movers with Creep ignore ZoC completely.
static inline bool inEnemyZoC(const Game& game, Pos p, UnitId moverUid) {
    if (moverUid == Map::kNoUnit) return false;
    if (!UnitSystem::unitExists(game, moverUid)) return false;

    // Creep movers ignore enemy zone-of-control completely.
    if (unitHasCreep(game, moverUid)) return false;

    const PlayerId moverOwner = UnitSystem::getOwnerId(game, moverUid);

    for (int i = 0; i < 8; ++i) {
        const Pos nb{ p.x + kNbDx8[i], p.y + kNbDy8[i] };
        if (!game.getMap().inBounds(nb)) continue;
        const UnitId occ = game.getMap().unitOn(nb);
        if (occ == Map::kNoUnit) continue;
        if (!UnitSystem::unitExists(game, occ)) continue;

        if (UnitSystem::getOwnerId(game, occ) == moverOwner) continue; // friendly units don't block
        if (unitHasHide(game, occ)) continue;                          // Hide units don't project ZoC

        return true;
    }

    return false;
}

// Returns true if p is a land/mountain/forest tile that is adjacent (8-neigh) to water or ocean.
static inline bool isCoastalLandTile(const Game& game, Pos p) {
    if (!game.getMap().inBounds(p)) return false;
    const Tile& t = game.getMap().at(p);
    const bool landish = (t.getBaseTerrain() == BaseTerrainEnum::Land ||
                          t.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                          t.getBaseTerrain() == BaseTerrainEnum::Forest);
    if (!landish) return false;

    // Coastal = touches Water or Ocean in 8-neighborhood.
    for (int i = 0; i < 8; ++i) {
        const Pos nb{ p.x + kNbDx8[i], p.y + kNbDy8[i] };
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
        if (!UnitSystem::unitExists(game, movingUnit)) return false;
        // Example hooks (uncomment if you have these skills):
        // if (UnitSystem::hasSkill(game, movingUnit, UnitSkill::Fly)) return false;
        // if (UnitSystem::hasSkill(game, movingUnit, UnitSkill::Creep)) return false;
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
    if (!UnitSystem::unitExists(game, movingUnit)) return true; // be conservative

    if (!game.getMap().inBounds(p)) return true;

    const Tile& t = game.getMap().at(p);
    const CityId cid = t.getTerritoryCityId();

    // Neutral (no city) => treat as allowed for road bonus.
    if (cid == kNoCity) return false;

    if (!CitySystem::cityExists(game, cid)) return true;

    return static_cast<PlayerId>(CitySystem::getCityOwner(game, cid)) != UnitSystem::getOwnerId(game, movingUnit);
}

static inline bool isTerminalAfterEntering(const Game& game, Pos p, UnitId moverUid, UnitId movingUnit, bool unitWaterCapable) {
    if (moverUid == Map::kNoUnit) return false;
    if (!UnitSystem::unitExists(game, moverUid)) return false;

    // WaterOnly / naval movement rule:
    // - Water-capable units may enter land ONLY to disembark, and must STOP immediately.
    //   This prevents walking multiple tiles inland in a single move.
    if (unitWaterCapable) {
        if (!game.getMap().inBounds(p)) return false;
        const Tile& t = game.getMap().at(p);
        const bool landish = (t.getBaseTerrain() == BaseTerrainEnum::Land ||
                              t.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                              t.getBaseTerrain() == BaseTerrainEnum::Forest);
        if (landish) {
            // Any time a water-capable unit enters land, it must stop.
            return true;
        }
    }

    // Terrain terminal rule:
    // Mountains and Forests are blocking/terminal tiles: you can enter them, but you cannot
    // continue moving from them during the same turn.
    {
        if (!game.getMap().inBounds(p)) return false;
        const Tile& t = game.getMap().at(p);
        const bool movingHasCreep = unitHasCreep(game, movingUnit);

        if (t.getBaseTerrain() == BaseTerrainEnum::Mountain) {
            return true;
        }

        // Forest is modeled as a resource on a land tile.
        // Forest WITHOUT a road is terminal; Forest WITH a road/bridge/settlement-road is NOT terminal.
        if (t.getBaseTerrain() == BaseTerrainEnum::Forest) {
            if (movingHasCreep) return false;
            if (!isRoadLikeTile(game, p, movingUnit)) {
                return true;
            }
        }    }

    // Enemy ZoC rule:
    // If you enter a tile that is within 1 of an enemy unit (that doesn't have Hide), you must STOP.
    // Hide units are not blocked and do not stop.
    if (inEnemyZoC(game, p, moverUid)) {
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

static Pos dirToDelta(int dx, int dy) {
    if (dx > 0 && dy == 0) return Pos{1,0};
    if (dx < 0 && dy == 0) return Pos{-1,0};
    if (dx == 0 && dy > 0) return Pos{0,1};
    if (dx == 0 && dy < 0) return Pos{0,-1};
    return Pos{0,1}; // default south
}

static Pos rotateCCW(Pos d) { return Pos{-d.y, d.x}; }
static Pos rotateCW(Pos d)  { return Pos{ d.y, -d.x}; }


bool MovementSystem::forceMove(Game& game, UnitId pushedUnit, Pos spawnPos) {
    if (!UnitSystem::unitExists(game, pushedUnit)) return false;

    const Pos from = spawnPos;

    // Precompute tech flags once (owner of the pushed unit).
    const PlayerId moverOwner = UnitSystem::getOwnerId(game, pushedUnit);
    const bool hasClimbing = PlayerSystem::hasTech(game, moverOwner, TechId::Climbing);
    const bool hasFishing  = PlayerSystem::hasTech(game, moverOwner, TechId::Fishing);
    const bool hasSailing  = PlayerSystem::hasTech(game, moverOwner, TechId::Sailing);

    // Direction preference rules:
    // - If the unit moved this turn: prefer its last move direction (friendly) / opposite (enemy).
    // - Else if it attacked this turn: prefer its last attack direction.
    // - Else: prefer its stored last move direction if present (even if it happened on a previous turn).
    // - If we still have no direction, fall back to pushing toward the map center.
    Pos dir{0,0};

    const Pos storedMove = UnitSystem::getLastMoveDir(game, pushedUnit);
    const Pos storedAtk  = UnitSystem::getLastAttackDir(game, pushedUnit);

    if (UnitSystem::movedThisTurn(game, pushedUnit)) {
        dir = (UnitSystem::getOwnerId(game, pushedUnit) == game.getCurrentPlayerId()) ? storedMove : Pos{-storedMove.x, -storedMove.y};
    } else if (UnitSystem::attackedThisTurn(game, pushedUnit)) {
        dir = storedAtk;
    } else if (!(storedMove.x == 0 && storedMove.y == 0)) {
        dir = storedMove;
    } else {
        const int cx = game.getMap().getWidth() / 2;
        const int cy = game.getMap().getHeight() / 2;
        dir = dirToDelta(cx - from.x, cy - from.y);
    }

    if (dir.x == 0 && dir.y == 0) dir = Pos{0,1}; // south fallback

    const bool unitWaterCapable = isWaterMover(game, pushedUnit);

    auto passableForPush = [&](Pos to) -> bool {
        if (!game.getMap().inBounds(to)) return false;
        if (game.getMap().unitOn(to) != Map::kNoUnit) return false;

        const Tile& tt = game.getMap().at(to);

        // Mountain requires Climbing.
        if (tt.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool toIsWater   = (tt.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool toIsOcean   = (tt.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool toIsPort    = (tt.getBuildingType() == BuildingTypeEnum::Port);
        const bool toIsLandish = (tt.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  tt.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                  tt.getBaseTerrain() == BaseTerrainEnum::Forest);
        const bool toIsBridge  = (tt.getRoadBridge() == RoadBridgeEnum::Bridge);

        // Ocean requires Sailing (even for water-capable units).
        if (toIsOcean && !hasSailing) return false;

        // Water-capable units may be pushed onto water; onto land only if coastal.
        if (unitWaterCapable) {
            if (toIsLandish) {
                if (!isCoastalLandTile(game, to)) return false;
            }
        } else {
            // Land units: cannot be pushed onto Water/Ocean unless it's a valid owned Port (requires Fishing)
            // OR a Bridge on Water (Polytopia-style).
            if ((toIsWater || toIsOcean) && !(toIsPort || (toIsBridge && toIsWater))) {
                return false;
            }
        }

        // Port tiles: always require Fishing and must be inside mover-owned city territory (+ Sailing if ocean).
        if (toIsPort) {
            if (!hasFishing) return false;

            const CityId tid = tt.getTerritoryCityId();
            if (tid == kNoCity) return false;
            if (!CitySystem::cityExists(game, tid)) return false;
            if (static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) != moverOwner) return false;

            if (toIsOcean && !hasSailing) return false;
        }

        return true;
    };

    // Try tiles: forward, then CCW/CW spiral.
    // Try ALL neighboring tiles; `dir` is only a preference.
    // Rank candidates by dot(dir, delta): best aligned with preferred direction first.
    std::array<Pos, 8> cand = {
        Pos{ 1, 0}, Pos{-1, 0}, Pos{0, 1}, Pos{0,-1},
        Pos{ 1, 1}, Pos{ 1,-1}, Pos{-1, 1}, Pos{-1,-1}
    };

    auto dot = [&](Pos a, Pos b) -> int { return a.x * b.x + a.y * b.y; };

    std::sort(cand.begin(), cand.end(), [&](const Pos& a, const Pos& b) {
        const int da = dot(dir, a);
        const int db = dot(dir, b);
        if (da != db) return da > db;
        const int ma = std::abs(a.x) + std::abs(a.y);
        const int mb = std::abs(b.x) + std::abs(b.y);
        if (ma != mb) return ma < mb;
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    for (const Pos d : cand) {
        const Pos to = Pos{from.x + d.x, from.y + d.y};
        if (!passableForPush(to)) continue;

        game.getMap().setUnitOn(from, Map::kNoUnit);
        game.getMap().setUnitOn(to, pushedUnit);
        UnitSystem::setPos(game, pushedUnit, to);

        // Fog-of-war: reveal around the forced-moved unit's new position.
        {
            const PlayerId pid = UnitSystem::getOwnerId(game, pushedUnit);
            const int baseVision = UnitSystem::getVisionRange(game, pushedUnit);
            const Pos pp = to;
            VisionSystem::revealArea(game, pid, pp, baseVision, RevealSource::Unit);

            const Tile& endTile = game.getMap().at(to);
            if (endTile.getBaseTerrain() == BaseTerrainEnum::Mountain) {
                VisionSystem::revealArea(game, pid, to, std::max(2, baseVision), RevealSource::Unit);
            }
            if (baseVision == 2) {
                VisionSystem::revealArea(game, pid, to, std::max(2, baseVision), RevealSource::Unit);
            }
        }

        // Disembark if an embarked carrier ends up on land.
        {
            const Tile& dst = game.getMap().at(to);
            const bool dstIsLandish = (dst.getBaseTerrain() == BaseTerrainEnum::Land ||
                                      dst.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                      dst.getBaseTerrain() == BaseTerrainEnum::Forest);

            if (dstIsLandish && UnitSystem::isEmbarked(game, pushedUnit)) {
                const UnitType original = UnitSystem::getEmbarkedBaseType(game, pushedUnit);
                if (original != UnitType::Unknown) {
                    Unit nu = UnitFactory::create(original, UnitSystem::getOwnerId(game, pushedUnit), to);
                    nu.setId(pushedUnit);

                    // Keep HP / status.
                    nu.setHealth(UnitSystem::getHealth(game, pushedUnit));
                    nu.setVeteran(UnitSystem::isVeteran(game, pushedUnit));
                    nu.setPoisoned(UnitSystem::isPoisoned(game, pushedUnit));
                    nu.setAttackedThisTurn(UnitSystem::attackedThisTurn(game, pushedUnit));

                    nu.clearEmbarkedBaseType();
                    nu.removeSkill(UnitSkill::WaterOnly);

                    // Disembarking ends movement/action for this turn.
                    nu.setMovedThisTurn(true);
                    nu.setAttackedThisTurn(true);

                    UnitSystem::replaceUnit(game, pushedUnit, nu);
                }
            }
        }

        // --- Stomp ---
        // Apply stomp at the landing tile (no retaliation, handled inside CombatSystem).
        CombatSystem::stompAt(game, pushedUnit, to);

        return true;
    }

    // No space -> remove unit entirely (no kill credit).
    game.getMap().setUnitOn(from, Map::kNoUnit);
    UnitSystem::setHealth(game, pushedUnit, 0);
    UnitSystem::setPos(game, pushedUnit, Pos{-9999, -9999});
    UnitSystem::setMovedThisTurn(game, pushedUnit, true);
    UnitSystem::setAttackedThisTurn(game, pushedUnit, true);

    return false;
}

// Forced push used by ruin/super-unit spawns. Returns true if unit survived.


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

        for (int i = 0; i < 8; ++i) {
            const Pos nb{ node.p.x + kNbDx8[i], node.p.y + kNbDy8[i] };
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
    if (!UnitSystem::unitExists(game, unitId)) return false;

    const int visionBeforeTransform = UnitSystem::getVisionRange(game, unitId);

    const Pos from = UnitSystem::getPos(game, unitId);
    if (from == to) return false;

    if (!game.getMap().inBounds(from) || !game.getMap().inBounds(to)) return false;

    // Default rule: you cannot move after you already moved.
    // Escape rule: units with Escape may move after attacking.
    if (UnitSystem::movedThisTurn(game, unitId)) return false;
    if (UnitSystem::attackedThisTurn(game, unitId) && !UnitSystem::hasSkill(game, unitId, UnitSkill::Escape)) return false;

    auto handleHiddenCloakEncounter = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;
        const UnitId occ = game.getMap().unitOn(p);
        if (occ == Map::kNoUnit) return false;
        if (!UnitSystem::unitExists(game, occ)) return false;

        const PlayerId moverOwner = UnitSystem::getOwnerId(game, unitId);
        const PlayerId occOwner = UnitSystem::getOwnerId(game, occ);
        if (occOwner == moverOwner) return false;
        if (!unitHasActivatedHide(game, occ)) return false;

        // Reveal the cloak after contact attempt (cloak-like detection behavior).
        UnitSystem::setLastMoveDir(game, occ, Pos{0, 0});
        return true;
    };

    {
        const UnitId occ = game.getMap().unitOn(to);
        if (occ != Map::kNoUnit) {
            // Entering a tile occupied by an enemy active-hide unit (cloak-like behavior)
            // bounces the mover back to its start and does not consume movement.
            if (handleHiddenCloakEncounter(to)) {
                return true;
            }
            return false;
        }
    }

    // Destination restrictions.
    {
        const Tile& tt = game.getMap().at(to);
        const Tile& tf = game.getMap().at(from);
        const PlayerId mover = UnitSystem::getOwnerId(game, unitId);

        // Fog-of-war: you may only END movement on a tile that is visible/revealed for you.
        if (!tileVisibleToPlayer(tt, mover)) return false;

        const bool hasClimbing = PlayerSystem::hasTech(game, mover, TechId::Climbing);
        const bool hasFishing  = PlayerSystem::hasTech(game, mover, TechId::Fishing);
        const bool hasSailing  = PlayerSystem::hasTech(game, mover, TechId::Sailing);

        // Mountains require Climbing
        if (tt.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool toIsWater = (tt.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool toIsOcean = (tt.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool toIsPort  = (tt.getBuildingType() == BuildingTypeEnum::Port);
        const bool toIsBridge = (tt.getRoadBridge() == RoadBridgeEnum::Bridge);

        const bool fromIsLandish = (tf.getBaseTerrain() == BaseTerrainEnum::Land ||
                                   tf.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                   tf.getBaseTerrain() == BaseTerrainEnum::Forest);

        const bool unitWaterCapable = isWaterMover(game, unitId);

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
                if (!CitySystem::cityExists(game, tid)) return false;
                if (static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) != mover) return false;

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
        const bool toIsLandish = (tt.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  tt.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                  tt.getBaseTerrain() == BaseTerrainEnum::Forest);
        if (unitWaterCapable && toIsLandish) {
            if (!isCoastalLandTile(game, to)) return false;
        }

        // PORT validation: entering a Port tile always requires Fishing and that the Port is on mover-owned city territory.
        // (applies to both land and water-capable units)
        if (toIsPort) {
            if (!hasFishing) return false;

            const CityId tid = tt.getTerritoryCityId();
            if (tid == kNoCity) return false;
            if (!CitySystem::cityExists(game, tid)) return false;
            if (static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) != mover) return false;

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

    const int mp = UnitSystem::getMovePoints(game, unitId);
    if (mp <= 0) return false;
    const int budgetHalf = mp * 2;

    const PlayerId moverOwner = UnitSystem::getOwnerId(game, unitId);

    // Precompute tech flags once.
    const bool hasClimbing = PlayerSystem::hasTech(game, moverOwner, TechId::Climbing);
    const bool hasFishing  = PlayerSystem::hasTech(game, moverOwner, TechId::Fishing);
    const bool hasSailing  = PlayerSystem::hasTech(game, moverOwner, TechId::Sailing);

    const bool unitWaterCapable = isWaterMover(game, unitId);

    auto canStepOn = [&](Pos p) -> bool {
        if (!map.inBounds(p)) return false;

        // Occupied tiles cannot be final destinations, but Creep may pass through them.
        // The starting tile is occupied by this moving unit.
        const UnitId occ = map.unitOn(p);
        if (occ != Map::kNoUnit) {
            if (p == from) {
                // ok
            } else {
                if (!UnitSystem::unitExists(game, occ)) return false; // be conservative
                if (UnitSystem::getOwnerId(game, occ) != moverOwner) {
                    if (unitHasCreep(game, unitId)) {
                        // Creep can jump/pass through occupied enemy tiles.
                    } else {
                        // Active-hide enemy is treated as unknown: path may traverse that tile.
                        if (!unitHasActivatedHide(game, occ)) {
                            return false; // visible enemy blocks
                        }
                    }
                }
                // friendly -> passable (destination is still required to be empty)
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
            if (!CitySystem::cityExists(game, tid)) return false;
            if (static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) != moverOwner) return false;

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
            if (curP != from && isTerminalAfterEntering(game, curP, unitId, unitId, unitWaterCapable)) {
                continue;
            }

            for (int i = 0; i < 8; ++i) {
                const Pos nb{ curP.x + kNbDx8[i], curP.y + kNbDy8[i] };
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
    const Pos moveDir{to.x - from.x, to.y - from.y};
    UnitSystem::setLastMoveDir(game, unitId, moveDir);
    UnitSystem::setPos(game, unitId, to);
    UnitSystem::setMovedThisTurn(game, unitId, true);

    // --- Infiltrate-on-enter ---
    // Entering an empty ENEMY city tile with an infiltrator triggers infiltration.
    // This must run AFTER we updated map occupancy and unit position.
    if (UnitSystem::unitExists(game, unitId) && UnitSystem::hasSkill(game, unitId, UnitSkill::Infiltrate)) {
        const Tile& tt = game.getMap().at(to);
        if (tt.getSettlementType() == SettlementTypeEnum::City) {
            const CityId cityId = static_cast<CityId>(tt.getSettlementId());
            if (CitySystem::cityExists(game, cityId)) {
                const PlayerId infiltrator = UnitSystem::getOwnerId(game, unitId);
                const PlayerId cityOwner   = static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId));
                if (infiltrator != kNoPlayer && cityOwner != kNoPlayer && cityOwner != infiltrator) {
                    // Infiltration system enforces siege/cooldown and consumes the infiltrator when successful.
                    if (InfiltrationSystem::infiltrateCity(game, unitId, cityId)) {
                        return true;
                    }
                }
            }
        }
    }

    bool stompAppliedDuringTransform = false;

    // ---- Port embark / disembark ----
    // Rule:
    // - When a LAND unit steps onto a PORT tile from a LAND tile, it becomes a Raft (or Juggernaut if Giant).
    // - When an embarked unit steps onto a LAND tile, it reverts to its stored original land type.
    {
        Tile& dst = game.getMap().at(to);
        Tile& src = game.getMap().at(from);

        const bool dstIsPort = (dst.getBuildingType() == BuildingTypeEnum::Port);
        const bool srcIsLandish = (src.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  src.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                  src.getBaseTerrain() == BaseTerrainEnum::Forest);
        const bool dstIsLandish = (dst.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  dst.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                  dst.getBaseTerrain() == BaseTerrainEnum::Forest);

        // Defensive: only the owning player with Fishing can embark via a Port.
        const PlayerId mover = UnitSystem::getOwnerId(game, unitId);
        const bool hasFishing = PlayerSystem::hasTech(game, mover, TechId::Fishing);
        bool portOwnedByMover = false;
        if (dstIsPort) {
            const CityId tid = dst.getTerritoryCityId();
            if (tid != kNoCity && CitySystem::cityExists(game, tid)) {
                portOwnedByMover = (static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) == mover);
            }
        }

        // Embark: land unit -> Raft/Juggernaut when stepping onto a Port from land.
        // Do NOT use isWaterCapable() here as the only gate, because some future unit types
        // may be water-capable without being an actual embarked carrier.
        const bool isAlreadyNavalType = isNavalType(UnitSystem::getType(game, unitId));

        if (dstIsPort && srcIsLandish && !isAlreadyNavalType && !UnitSystem::isEmbarked(game, unitId)) {
            if (hasFishing && portOwnedByMover) {
                const UnitType original = UnitSystem::getType(game, unitId);

                UnitType naval;
                if (original == UnitType::Giant) {
                    naval = UnitType::Juggernaut;
                } else if (original == UnitType::Cloak) {
                    naval = UnitType::Dinghy;
                } else {
                    naval = UnitType::Raft;
                }

                Unit nu = UnitFactory::create(naval, UnitSystem::getOwnerId(game, unitId), to);
                nu.setId(unitId);

                // Preserve HP and MaxHP from the land unit (Raft must inherit HP pool).
                const int oldMaxHp = UnitSystem::getMaxHealth(game, unitId);
                const int oldHp    = UnitSystem::getHealth(game, unitId);
                nu.setMaxHealth(oldMaxHp);
                nu.setHealth(std::min(oldHp, oldMaxHp));

                // Keep important runtime state.
                nu.setVeteran(UnitSystem::isVeteran(game, unitId));
                nu.setPoisoned(UnitSystem::isPoisoned(game, unitId));
                nu.setAttackedThisTurn(UnitSystem::attackedThisTurn(game, unitId));

                // Remember original unit type, mark as embarked carrier.
                nu.setEmbarkedBaseType(original);
                nu.addSkill(UnitSkill::WaterOnly);

                // IMPORTANT: embarking ends the unit's movement/action for this turn.
                nu.setMovedThisTurn(true);
                nu.setAttackedThisTurn(true);

                UnitSystem::replaceUnit(game, unitId, nu);
            }
        }

        // Disembark: embarked carrier stepping onto land -> revert to original land unit type.
        if (dstIsLandish && UnitSystem::isEmbarked(game, unitId)) {
            const UnitType original = UnitSystem::getEmbarkedBaseType(game, unitId);
            if (original != UnitType::Unknown) {
                if (UnitSystem::hasSkill(game, unitId, UnitSkill::Stomp)) {
                    for (size_t i = 1; i < path.size(); ++i) {
                        CombatSystem::stompAt(game, unitId, path[i]);
                    }
                    stompAppliedDuringTransform = true;
                }

                Unit nu = UnitFactory::create(original, UnitSystem::getOwnerId(game, unitId), to);
                nu.setId(unitId);

                // Keep HP / status.
                nu.setHealth(UnitSystem::getHealth(game, unitId));
                nu.setVeteran(UnitSystem::isVeteran(game, unitId));
                nu.setPoisoned(UnitSystem::isPoisoned(game, unitId));
                nu.setAttackedThisTurn(UnitSystem::attackedThisTurn(game, unitId));

                nu.clearEmbarkedBaseType();
                nu.removeSkill(UnitSkill::WaterOnly);

                // IMPORTANT: disembarking ends movement/action for this turn.
                nu.setMovedThisTurn(true);
                nu.setAttackedThisTurn(true);

                UnitSystem::replaceUnit(game, unitId, nu);
            }
        }
    }

    // --- Stomp ---
    // Deal stomp damage around the unit for every step along the movement path (excluding the starting tile).
    // IMPORTANT: stomp does NOT cause retaliation.
    if (!stompAppliedDuringTransform &&
        UnitSystem::unitExists(game, unitId) &&
        UnitSystem::hasSkill(game, unitId, UnitSkill::Stomp)) {
        for (size_t i = 1; i < path.size(); ++i) {
            CombatSystem::stompAt(game, unitId, path[i]);
        }
    }

    const int visionAfterTransform = UnitSystem::getVisionRange(game, unitId);
    const int revealVision = std::max(visionBeforeTransform, visionAfterTransform);

    // Fog-of-war: reveal tiles the unit passed through (whole path) using the unit's vision range.
    // If the unit ends on a Mountain tile, ensure at least radius 2 from the destination.
    const PlayerId pid = UnitSystem::getOwnerId(game, unitId);

    // Reveal along the whole traversed path with "best" vision (e.g. sailor->land keeps 2 for this move).
    for (const Pos& pp : path) {
        VisionSystem::revealArea(game, pid, pp, revealVision, RevealSource::Unit);
    }

    // Always reveal at final position again (safety + requirement)
    VisionSystem::revealArea(game, pid, to, revealVision, RevealSource::Unit);

    // Mountain bonus at destination (still using revealVision)
    const Tile& endTile = game.getMap().at(to);
    if (endTile.getBaseTerrain() == BaseTerrainEnum::Mountain) {
        VisionSystem::revealArea(game, pid, to, std::max(2, revealVision), RevealSource::Unit);
    }

    return true;
}

std::vector<Pos> MovementSystem::reachable(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) return {};

    // Default rule: no movement after the unit has moved.
    // Escape rule: units with Escape may move after attacking.
    if (UnitSystem::movedThisTurn(game, unitId)) return {};
    if (UnitSystem::attackedThisTurn(game, unitId) && !UnitSystem::hasSkill(game, unitId, UnitSkill::Escape)) return {};

    const Pos start = UnitSystem::getPos(game, unitId);
    if (!game.getMap().inBounds(start)) return {};

    const int mp = UnitSystem::getMovePoints(game, unitId);
    if (mp <= 0) return {};

    const int w = game.getMap().getWidth();
    const int h = game.getMap().getHeight();
    const int n = w * h;

    const int budgetHalf = mp * 2;
    const int INF = std::numeric_limits<int>::max() / 4;

    std::vector<Pos> out;
    out.reserve(128);

    const PlayerId moverOwner = UnitSystem::getOwnerId(game, unitId);
    const bool hasClimbing = game.getPlayer(moverOwner).hasTech(TechId::Climbing);
    const bool hasFishing  = game.getPlayer(moverOwner).hasTech(TechId::Fishing);
    const bool hasSailing  = game.getPlayer(moverOwner).hasTech(TechId::Sailing);
    const bool unitWaterCapable = isWaterMover(game, unitId);

    auto canStep = [&](Pos p) -> bool {
        if (!game.getMap().inBounds(p)) return false;

        // Occupied tiles cannot be final destinations, but Creep may pass through them.
        // The starting tile is occupied by this moving unit.
        const UnitId occ = game.getMap().unitOn(p);
        if (occ != Map::kNoUnit) {
            if (p == start) {
                // ok
            } else {
                if (!UnitSystem::unitExists(game, occ)) return false; // be conservative
                if (UnitSystem::getOwnerId(game, occ) != moverOwner) {
                    if (unitHasCreep(game, unitId)) {
                        // Creep can jump/pass through occupied enemy tiles.
                    } else {
                        // Active-hide enemy is treated as "unknown occupant":
                        // you may target that tile, but movement cannot continue through it.
                        if (!unitHasActivatedHide(game, occ)) {
                            return false; // visible enemy blocks
                        }
                    }
                }
                // friendly -> passable
            }
        }
        const Tile& t = game.getMap().at(p);

        // Mountain restriction: cannot enter mountains without Climbing.
        if (t.getBaseTerrain() == BaseTerrainEnum::Mountain && !hasClimbing) return false;

        const bool toIsWater   = (t.getBaseTerrain() == BaseTerrainEnum::Water);
        const bool toIsOcean   = (t.getBaseTerrain() == BaseTerrainEnum::Ocean);
        const bool toIsPort    = (t.getBuildingType() == BuildingTypeEnum::Port);
        const bool toIsLandish = (t.getBaseTerrain() == BaseTerrainEnum::Land ||
                                  t.getBaseTerrain() == BaseTerrainEnum::Mountain ||
                                  t.getBaseTerrain() == BaseTerrainEnum::Forest);
        const bool toIsBridge  = (t.getRoadBridge() == RoadBridgeEnum::Bridge);

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
            if (!CitySystem::cityExists(game, tid)) return false;
            if (static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) != moverOwner) return false;

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
            const UnitId occCur = game.getMap().unitOn(curP);
            const bool occCurExists = (occCur != Map::kNoUnit) && UnitSystem::unitExists(game, occCur);
            const bool curHasEnemyActiveHide =
                occCurExists &&
                (UnitSystem::getOwnerId(game, occCur) != moverOwner) &&
                unitHasActivatedHide(game, occCur);

            if (curP != start) {
                const bool occupiedByBlockingUnit =
                    (occCur != Map::kNoUnit) && !curHasEnemyActiveHide;
                if (!occupiedByBlockingUnit) {
                    const Tile& tn = game.getMap().at(curP);
                    if (tileVisibleToPlayer(tn, moverOwner)) {
                        // Dedup without sorting.
                        if (g_bucket.outStamp[static_cast<size_t>(ci)] != g_bucket.outEpoch) {
                            g_bucket.outStamp[static_cast<size_t>(ci)] = g_bucket.outEpoch;
                            out.push_back(curP);
                        }
                    }
                }
            }

            // Terminal tile: you can END here, but you can't move further this turn.
            if (curP != start && isTerminalAfterEntering(game, curP, unitId, unitId, unitWaterCapable)) {
                continue;
            }
            for (int i = 0; i < 8; ++i) {
                const Pos nb{ curP.x + kNbDx8[i], curP.y + kNbDy8[i] };
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
