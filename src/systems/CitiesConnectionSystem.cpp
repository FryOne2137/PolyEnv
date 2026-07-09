// Created by Fryderyk Niedzwiecki on 02/02/2026.

#include "CitiesConnectionSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>


#include "../game/Game.h"
#include "systems/CitySystem.h"
#include "systems/PlayerSystem.h"
#include "world/Tile.h"
#include "world/Map.h"
#include "systems/MonumentSystem.h"


namespace {

// 8-direction adjacency (your project uses diagonal adjacency in multiple places).
static const std::array<Pos, 8> kNb8 = {
    Pos{ 1, 0}, Pos{-1, 0}, Pos{0, 1}, Pos{0,-1},
    Pos{ 1, 1}, Pos{-1, 1}, Pos{1,-1}, Pos{-1,-1}
};

template <class F>
inline void forEachNb8(Pos p, F&& f) {
    for (const Pos d : kNb8) {
        f(Pos{p.x + d.x, p.y + d.y});
    }
}

inline bool isEnemyTerritory(const Game& game, const Tile& t, PlayerId pid) {
    const CityId tid = t.getTerritoryCityId();
    if (tid == kNoCity) return false;
    if (!CitySystem::cityExists(game, tid)) return false;
    return static_cast<PlayerId>(CitySystem::getCityOwner(game, tid)) != pid;
}

inline bool isVisibleForConn(const Tile&, PlayerId) {
    // If you have fog-of-war visibility, plug it in here.
    return true;
}

inline bool isPort(const Tile& t) {
    return t.getBuildingType() == BuildingTypeEnum::Port;
}

inline bool isWaterTile(const Tile& t) {
    const auto bt = t.getBaseTerrain();
    return bt == BaseTerrainEnum::Water || bt == BaseTerrainEnum::Ocean;
}


inline bool isLinkTileForPlayerIdx(const Tile& t, PlayerId pid, int idx, const std::vector<uint8_t>& waterOwner) {
    const auto rb = t.getRoadBridge();
    if (rb == RoadBridgeEnum::Road || rb == RoadBridgeEnum::Bridge) return true;
    if (rb == RoadBridgeEnum::WaterConnection) {
        if (idx < 0 || static_cast<size_t>(idx) >= waterOwner.size()) return false;
        return waterOwner[static_cast<size_t>(idx)] == static_cast<uint8_t>(pid);
    }
    return false;
}


inline bool isConnNodeForPlayerIdx(const Tile& t, PlayerId pid, int idx, const std::vector<uint8_t>& waterOwner) {
    // A connection network node is either:
    // - a road/bridge
    // - a player-owned WaterConnection
    // - a port tile
    return isPort(t) || isLinkTileForPlayerIdx(t, pid, idx, waterOwner);
}


inline bool canTraverseConn(const Game& game, PlayerId pid, Pos p, const std::vector<uint8_t>& waterOwner) {
    const Map& map = game.getMap();
    if (!map.inBounds(p)) return false;
    const int idx = map.index(p);
    const Tile& t = map.at(p);

    if (!isConnNodeForPlayerIdx(t, pid, idx, waterOwner)) return false;
    if (isEnemyTerritory(game, t, pid)) return false;
    if (!isVisibleForConn(t, pid)) return false;
    return true;
}

static void computeVisitedNetwork(
    const Game& game,
    PlayerId pid,
    Pos capitalPos,
    const std::vector<uint8_t>& waterOwner,
    std::vector<uint32_t>& stamp,
    uint32_t epoch,
    std::vector<int>& q
) {
    const Map& map = game.getMap();
    const int W = map.getWidth();

    auto tryPush = [&](Pos p, int parentIdx) {
        if (!map.inBounds(p)) return;
        if (!canTraverseConn(game, pid, p, waterOwner)) return;
        const int idx = map.index(p);
        if (stamp[static_cast<size_t>(idx)] == epoch) return;
        stamp[static_cast<size_t>(idx)] = epoch;
        q.push_back(idx);
        (void)parentIdx;
    };

    q.clear();

    // Seed: capital tile itself (if it is a node), plus its neighbors.
    if (map.inBounds(capitalPos)) {
        if (canTraverseConn(game, pid, capitalPos, waterOwner)) {
            const int i0 = map.index(capitalPos);
            stamp[static_cast<size_t>(i0)] = epoch;
            q.push_back(i0);
        }
    }

    forEachNb8(capitalPos, [&](Pos nb) {
        tryPush(nb, -1);
    });

    // BFS using index queue.
    size_t head = 0;
    while (head < q.size()) {
        const int curIdx = q[head++];
        const int x = curIdx % W;
        const int y = curIdx / W;
        const Pos curP{x, y};

        forEachNb8(curP, [&](Pos nb) {
            tryPush(nb, curIdx);
        });
    }
}

static bool cityIsConnected(
    const Game& game,
    PlayerId pid,
    const std::vector<uint8_t>& waterOwner,
    const std::vector<uint32_t>& stamp,
    uint32_t epoch,
    Pos cityPos
) {
    const Map& map = game.getMap();

    auto isVisited = [&](Pos p) -> bool {
        if (!map.inBounds(p)) return false;
        const int idx = map.index(p);
        return stamp[static_cast<size_t>(idx)] == epoch;
    };

    // Connected if city tile itself is a node and visited, or any adjacent node is visited.
    if (map.inBounds(cityPos)) {
        const Tile& ct = map.at(cityPos);
        if (isConnNodeForPlayerIdx(ct, pid, map.index(cityPos), waterOwner) && isVisited(cityPos)) return true;
    }

    bool ok = false;
    forEachNb8(cityPos, [&](Pos nb) {
        if (ok) return;
        if (!map.inBounds(nb)) return;
        const Tile& t = map.at(nb);
        if (!isConnNodeForPlayerIdx(t, pid, map.index(nb), waterOwner)) return;
        if (isVisited(nb)) ok = true;
    });

    return ok;
}

static inline void applyPopDelta(Game& game, CityId cid, int delta) {
    if (cid == kNoCity || delta == 0) return;
    if (!CitySystem::cityExists(game, cid)) return;

    if (delta > 0) {
        const int inc = std::min(delta, 255);
        (void)CitySystem::addPopulation(game, cid, static_cast<uint16_t>(inc));
        return;
    }

    // Negative: signed-safe.
    const int cur = static_cast<int>(CitySystem::getCityPopulation(game, cid));
    const int next = cur + delta;
    const int clamped = std::clamp(next, -32768, 32767);
    (void)CitySystem::setCityPopulation(game, cid, static_cast<int16_t>(clamped));
}

} // namespace

void CitiesConnectionSystem::update(Game& game) {
    Map& map = game.getMap();
    const int W = map.getWidth();
    const int H = map.getHeight();
    const int total = W * H;

    // Previous connected cities (excluding capital) per player.
    static std::unordered_map<int, std::unordered_set<int>> s_connected;

    // Track previous capital per player. If capital changes (e.g. capture), reset connection-diff state
    // to avoid applying spurious +/- population deltas.
    static std::unordered_map<int, int> s_prevCapital;

    // Track which water tiles we set for each player so we can clear only those.
    static std::unordered_map<int, std::vector<int>> s_waterConnTiles;

    // Stamp-based visited set.
    static std::vector<uint32_t> s_stamp;
    static uint32_t s_epoch = 1;
    static std::vector<int> s_queue;

    // Ownership for WaterConnection tiles (255 = none). Tile stores only presence.
    static std::vector<uint8_t> s_waterOwner;

    if (static_cast<int>(s_stamp.size()) != total) {
        s_stamp.assign(static_cast<size_t>(std::max(0, total)), 0u);
        s_epoch = 1;
    }
    if (static_cast<int>(s_waterOwner.size()) != total) {
        s_waterOwner.assign(static_cast<size_t>(std::max(0, total)), 0xFF);
    }

    auto nextEpoch = [&]() {
        if (++s_epoch == 0) {
            std::fill(s_stamp.begin(), s_stamp.end(), 0u);
            s_epoch = 1;
        }
    };

    // Collect city IDs by scanning territory markers (and settlement fallback).
    std::unordered_set<CityId> allCityIds;
    allCityIds.reserve(64);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Pos p{x, y};
            const Tile& t = map.at(p);
            CityId cid = t.getTerritoryCityId();

            if (cid == kNoCity && t.getSettlementType() == SettlementTypeEnum::City) {
                const CityId guess = static_cast<CityId>(t.getSettlementId());
                if (guess != kNoCity && CitySystem::cityExists(game, guess)) {
                    cid = guess;
                }
            }

            if (cid == kNoCity) continue;
            if (!CitySystem::cityExists(game, cid)) continue;
            allCityIds.insert(cid);
        }
    }

    struct OwnerInfo {
        CityId capital = kNoCity;
        Pos capitalPos{0, 0};
        std::vector<CityId> cities;
    };

    std::unordered_map<PlayerId, OwnerInfo> owners;

    // Collect city lists by owner via CitySystem.
    for (const CityId cid : allCityIds) {
        if (!CitySystem::cityExists(game, cid)) continue;
        const PlayerId pid = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));
        owners[pid].cities.push_back(cid);
    }

    // Determine capitals from Player state (single source of truth).
    for (auto& [pid, info] : owners) {
        const CityId capId = PlayerSystem::getCapitalId(game, pid);
        if (capId != kNoCity &&
            CitySystem::cityExists(game, capId) &&
            static_cast<PlayerId>(CitySystem::getCityOwner(game, capId)) == pid) {
            info.capital = capId;
            info.capitalPos = CitySystem::getCityPos(game, capId);
        }
    }

    // If any player has no capital flagged, skip them (your Game should set capitals).
    for (auto& [pid, info] : owners) {
        if (info.capital == kNoCity) continue;
        const CityId capitalCid = info.capital;
        if (capitalCid == kNoCity) continue;
        if (!CitySystem::cityExists(game, capitalCid)) continue;

        // 1) Clear old water connections for this player.
        {
            auto& lst = s_waterConnTiles[static_cast<int>(pid)];
            for (const int idx : lst) {
                const int x = idx % W;
                const int y = idx / W;
                const Pos p{x, y};
                if (!map.inBounds(p)) continue;
                Tile& tt = map.at(p);
                if (tt.getRoadBridge() == RoadBridgeEnum::WaterConnection) {
                    // Clear only if this system says it's ours.
                    if (idx >= 0 && static_cast<size_t>(idx) < s_waterOwner.size() &&
                        s_waterOwner[static_cast<size_t>(idx)] == static_cast<uint8_t>(pid)) {
                        tt.setRoadBridge(RoadBridgeEnum::None);
                        s_waterOwner[static_cast<size_t>(idx)] = 0xFF;
                    }
                }
            }
            lst.clear();
        }

        // 2) Land+port reachability from capital.
        nextEpoch();
        computeVisitedNetwork(game, pid, info.capitalPos, s_waterOwner, s_stamp, s_epoch, s_queue);

        auto isVisited = [&](Pos p) -> bool {
            if (!map.inBounds(p)) return false;
            const int idx = map.index(p);
            return s_stamp[static_cast<size_t>(idx)] == s_epoch;
        };

        auto tileIsOwnedPortOfPid = [&](Pos p) -> bool {
            if (!map.inBounds(p)) return false;
            const Tile& t = map.at(p);
            if (!isPort(t)) return false;
            const CityId tcid = t.getTerritoryCityId();
            if (tcid == kNoCity) return false;
            if (!CitySystem::cityExists(game, tcid)) return false;
            return static_cast<PlayerId>(CitySystem::getCityOwner(game, tcid)) == pid;
        };

        // 3) Collect ALL owned ports (even if the port network is not connected to the capital).
        // This ensures port-to-port WaterConnection threads are created locally.
        std::vector<Pos> ownedPorts;
        ownedPorts.reserve(16);
        for (int y2 = 0; y2 < H; ++y2) {
            for (int x2 = 0; x2 < W; ++x2) {
                const Pos p{x2, y2};
                const Tile& t = map.at(p);
                if (!isPort(t)) continue;
                // Only consider  ports that belong to this player (skip enemy/neutral).
                if (!tileIsOwnedPortOfPid(p)) continue;
                ownedPorts.push_back(p);
            }
        }

        // Ensure we create at most ONE connection per unordered port pair.
        // We will only mark a path when startPortIdx < destPortIdx.
        std::unordered_set<int> ownedPortIdx;
        ownedPortIdx.reserve(ownedPorts.size());
        for (const Pos p : ownedPorts) {
            ownedPortIdx.insert(map.index(p));
        }

        // Port max distance per wiki: "no more than four water tiles away".
        // We implement this as max steps <= 5 including the destination port tile.
        constexpr int kMaxPortSteps = 5;

        // Reusable BFS buffers for water routing (bounded by small kMaxPortSteps).
        static std::vector<int> s_parent;
        static std::vector<uint16_t> s_dist;
        static std::vector<int> s_q;
        if (static_cast<int>(s_parent.size()) != total) {
            s_parent.assign(static_cast<size_t>(std::max(0, total)), -1);
            s_dist.assign(static_cast<size_t>(std::max(0, total)), 0xFFFF);
        }

        auto markWaterPath = [&](const std::vector<int>& pathIdx) {
            auto& lst = s_waterConnTiles[static_cast<int>(pid)];
            for (const int idx : pathIdx) {
                const int x = idx % W;
                const int y = idx / W;
                const Pos p{x, y};
                if (!map.inBounds(p)) continue;
                Tile& tt = map.at(p);

                // Allow marking both water tiles and the endpoint Port tiles.
                if (!isWaterTile(tt) && !isPort(tt)) continue;
                if (tt.getRoadBridge() == RoadBridgeEnum::Road || tt.getRoadBridge() == RoadBridgeEnum::Bridge) continue;

                if (tt.getRoadBridge() == RoadBridgeEnum::WaterConnection) {
                    const uint8_t owner = (idx >= 0 && static_cast<size_t>(idx) < s_waterOwner.size())
                        ? s_waterOwner[static_cast<size_t>(idx)]
                        : 0xFF;
                    // Do not overwrite another player's thread.
                    if (owner != 0xFF && owner != static_cast<uint8_t>(pid)) {
                        continue;
                    }
                    // Already ours.
                    if (owner == static_cast<uint8_t>(pid)) {
                        continue;
                    }
                }

                tt.setRoadBridge(RoadBridgeEnum::WaterConnection);
                if (idx >= 0 && static_cast<size_t>(idx) < s_waterOwner.size()) {
                    s_waterOwner[static_cast<size_t>(idx)] = static_cast<uint8_t>(pid);
                }
                lst.push_back(idx);
            }
        };

        // 4) For each owned port, connect to other owned ports within range by shortest water path.
        for (const Pos startPort : ownedPorts) {
            std::fill(s_parent.begin(), s_parent.end(), -1);
            std::fill(s_dist.begin(), s_dist.end(), 0xFFFF);
            s_q.clear();

            const int sIdx = map.index(startPort);
            s_parent[static_cast<size_t>(sIdx)] = sIdx;
            s_dist[static_cast<size_t>(sIdx)] = 0;
            s_q.push_back(sIdx);

            size_t head = 0;
            while (head < s_q.size()) {
                const int curIdx = s_q[head++];
                const uint16_t cd = s_dist[static_cast<size_t>(curIdx)];
                if (cd >= kMaxPortSteps) continue;

                const int cx = curIdx % W;
                const int cy = curIdx / W;
                const Pos curP{cx, cy};

                // Do not traverse THROUGH ports; a port can be an endpoint only.
                // Otherwise BFS can chain across many ports on land and create invalid/ugly water threads.
                if (curIdx != sIdx) {
                    const Tile& curT = map.at(curP);
                    if (isPort(curT)) {
                        continue;
                    }
                }

                forEachNb8(curP, [&](Pos nb) {
                    if (!map.inBounds(nb)) return;
                    const int ni = map.index(nb);
                    if (s_dist[static_cast<size_t>(ni)] != 0xFFFF) return;

                    const Tile& nt = map.at(nb);

                    const bool nbIsPort = isPort(nt);

                    // Water routing domain: water/ocean tiles, plus OWNED ports as endpoints.
                    if (nbIsPort) {
                        // Never enter enemy/neutral ports during water BFS (prevents hopping).
                        if (!tileIsOwnedPortOfPid(nb)) return;
                    } else {
                        if (!isWaterTile(nt)) return;
                    }

                    if (isEnemyTerritory(game, nt, pid)) return;

                    s_dist[static_cast<size_t>(ni)] = static_cast<uint16_t>(cd + 1);
                    s_parent[static_cast<size_t>(ni)] = curIdx;
                    s_q.push_back(ni);

                    // Reached another owned port within range => mark ONE shortest path per unordered port pair.
                    if (nbIsPort) {
                        const int destIdx = ni;
                        // Only connect to ports that are in our owned port set.
                        if (ownedPortIdx.find(destIdx) == ownedPortIdx.end()) return;
                        // Create the connection only once for the unordered pair (start,dest).
                        if (sIdx >= destIdx) return;

                        // Reconstruct indices INCLUDING the destination port and the start port,
                        // so WaterConnection is also present "under" both ports.
                        std::vector<int> pathIdx;
                        int walk = destIdx;
                        while (walk != sIdx && walk != -1) {
                            pathIdx.push_back(walk);
                            walk = s_parent[static_cast<size_t>(walk)];
                        }
                        // Include the start port tile too.
                        pathIdx.push_back(sIdx);
                        markWaterPath(pathIdx);
                    }
                });
            }
        }

        // 5) Recompute reachability including WaterConnection tiles.
        nextEpoch();
        computeVisitedNetwork(game, pid, info.capitalPos, s_waterOwner, s_stamp, s_epoch, s_queue);

        // 6) Build connected-city set and diff population.
        std::unordered_set<CityId> now;
        for (const CityId cid : info.cities) {
            if (cid == info.capital) continue;
            if (!CitySystem::cityExists(game, cid)) continue;

            if (cityIsConnected(game, pid, s_waterOwner, s_stamp, s_epoch, CitySystem::getCityPos(game, cid))) {
                now.insert(cid);
            }
        }

        // Monument unlock: connect 5 cities to the capital (i.e. 5 non-capital cities are connected).
        MonumentSystem::onConnectedCitiesUpdated(game, pid, now.size());

        auto& prev = s_connected[static_cast<int>(pid)];

        // If the capital changed since last update (most notably after capturing a capital),
        // clear the previous connected set so we don't apply an immediate "lost connections" penalty.
        {
            const int pidKey = static_cast<int>(pid);
            const int curCap = static_cast<int>(info.capital);
            auto it = s_prevCapital.find(pidKey);
            if (it == s_prevCapital.end() || it->second != curCap) {
                prev.clear();
                s_prevCapital[pidKey] = curCap;
            }
        }

        // New connections.
        for (const CityId cid : now) {
            if (prev.find(static_cast<int>(cid)) != prev.end()) continue;
            applyPopDelta(game, cid, +1);
            applyPopDelta(game, capitalCid, +1);
        }

        // Lost connections.
        for (const int prevCid : prev) {
            if (now.find(static_cast<CityId>(prevCid)) != now.end()) continue;
            const CityId lostCid = static_cast<CityId>(prevCid);
            applyPopDelta(game, lostCid, -1);
            applyPopDelta(game, capitalCid, -1);
        }

        prev.clear();
        for (const CityId cid : now) prev.insert(static_cast<int>(cid));
    }
}
