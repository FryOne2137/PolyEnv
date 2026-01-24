//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#include "MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

static constexpr float BORDER_EXPANSION = 1.0f / 3.0f;

// Static storage for last generated capitals
std::vector<Pos> MapGenerator::s_lastCapitals{};

int MapGenerator::randInt(std::mt19937& rng, int a, int b) {
    std::uniform_int_distribution<int> dist(a, b);
    return dist(rng);
}

float MapGenerator::rand01(std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

int MapGenerator::chebyshevDistance(int a, int b, int size) {
    int ax = a % size, ay = a / size;
    int bx = b % size, by = b / size;
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

std::vector<int> MapGenerator::circle(int center, int radius, int size) {
    std::vector<int> out;
    int row = center / size;
    int col = center % size;

    int i = row - radius;
    if (0 <= i && i < size) {
        for (int j = col - radius; j <= col + radius; ++j) {
            if (0 <= j && j < size) out.push_back(i * size + j);
        }
    }

    i = row + radius;
    if (0 <= i && i < size) {
        for (int j = col + radius; j >= col - radius; --j) {
            if (0 <= j && j < size) out.push_back(i * size + j);
        }
    }

    int j = col - radius;
    if (0 <= j && j < size) {
        for (int ii = row + radius; ii >= row - radius; --ii) {
            if (0 <= ii && ii < size) out.push_back(ii * size + j);
        }
    }

    j = col + radius;
    if (0 <= j && j < size) {
        for (int ii = row - radius; ii <= row + radius; ++ii) {
            if (0 <= ii && ii < size) out.push_back(ii * size + j);
        }
    }

    // dedup (python dodaje narożniki parę razy)
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<int> MapGenerator::round(int center, int radius, int size) {
    std::vector<int> out;
    out.reserve((radius * 8) + 1);
    for (int r = 1; r <= radius; ++r) {
        auto c = circle(center, r, size);
        out.insert(out.end(), c.begin(), c.end());
    }
    out.push_back(center);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<int> MapGenerator::plusSign(int center, int size) {
    std::vector<int> out;
    int row = center / size;
    int col = center % size;
    if (col > 0) out.push_back(center - 1);
    if (col < size - 1) out.push_back(center + 1);
    if (row > 0) out.push_back(center - size);
    if (row < size - 1) out.push_back(center + size);
    return out;
}

bool MapGenerator::isLandLike(BaseTerrainEnum t) {
    return t == BaseTerrainEnum::Land || t == BaseTerrainEnum::Mountain;
}

MapGenerator::TribeProbs MapGenerator::probsForTribe(TribeType tribe) {
    // To są wartości “w duchu” Twojego Pythona (te same relacje).
    TribeProbs p{};

    switch (tribe) {
        case TribeType::Kickoo:   p.water = 0.40f; p.fish = 1.50f; break;
        case TribeType::Aquarion: p.water = 0.30f; p.fish = 1.20f; break;
        case TribeType::Bardur:   p.forest = 1.00f; p.animals = 2.00f; break;
        case TribeType::XinXi:    p.mountain = 1.50f; break;
        case TribeType::AiMo:     p.mountain = 1.50f; break;
        case TribeType::Oumaji:   p.forest = 0.10f; break;
        case TribeType::Vengir:   p.metal = 2.00f; break;
        default: break;
    }

    return p;
}

void MapGenerator::generate(Map& map, const Params& params) {
    const int N = params.mapSize;
    if (map.getWidth() != N || map.getHeight() != N) {
        map.init(N, N);
    }

    // RNG
    std::mt19937 rng;
    if (params.seed == 0) {
        std::random_device rd;
        rng.seed(rd());
    } else {
        rng.seed(params.seed);
    }

    const auto& tribes = map.getActiveTribes();
    if (tribes.size() < 2 || tribes.size() > 16) {
        return;
    }

    // 1) Start: ocean everywhere
    for (int i = 0; i < N * N; ++i) {
        Tile& t = map.allTiles()[static_cast<size_t>(i)];
        t.setBaseTerrain(BaseTerrainEnum::Ocean);
        t.setResource(ResourcesEnum::None);
        t.clearSettlement();
        t.setBuildingType(BuildingTypeEnum::None);
        t.setRoadBridge(RoadBridgeEnum::None);
        t.setTribe(TribeType::Unknown);
    }

    // 2) Initial land blobs (random seeds)
    int landTargets = static_cast<int>(std::round(float(N * N) * params.initialLand));
    int placed = 0;
    while (placed < landTargets) {
        int cell = randInt(rng, 0, N * N - 1);
        Tile& t = map.allTiles()[static_cast<size_t>(cell)];
        if (t.getBaseTerrain() == BaseTerrainEnum::Ocean) {
            t.setBaseTerrain(BaseTerrainEnum::Land);
            placed++;
        }
    }

    // 3) Smoothing like python
    float landCoefficient = (0.5f + float(params.relief)) / 9.0f;

    std::vector<uint8_t> makeLand(static_cast<size_t>(N * N), 0);

    for (int it = 0; it < params.smoothing; ++it) {
        for (int cell = 0; cell < N * N; ++cell) {
            int waterCount = 0;
            int tileCount = 0;
            auto neigh = round(cell, 1, N);
            for (int nb : neigh) {
                if (map.allTiles()[static_cast<size_t>(nb)].getBaseTerrain() == BaseTerrainEnum::Ocean) {
                    waterCount++;
                }
                tileCount++;
            }
            makeLand[static_cast<size_t>(cell)] =
                (tileCount > 0 && (float(waterCount) / float(tileCount) <= landCoefficient)) ? 1 : 0;
        }

        for (int cell = 0; cell < N * N; ++cell) {
            Tile& t = map.allTiles()[static_cast<size_t>(cell)];
            t.setBaseTerrain(makeLand[static_cast<size_t>(cell)] ? BaseTerrainEnum::Land : BaseTerrainEnum::Ocean);
        }
    }

    // 4) Convert ocean adjacent to land into shallow water
    for (int cell = 0; cell < N * N; ++cell) {
        Tile& t = map.allTiles()[static_cast<size_t>(cell)];
        if (t.getBaseTerrain() != BaseTerrainEnum::Ocean) continue;

        for (int nb : plusSign(cell, N)) {
            BaseTerrainEnum nt = map.allTiles()[static_cast<size_t>(nb)].getBaseTerrain();
            if (nt == BaseTerrainEnum::Land || nt == BaseTerrainEnum::Mountain) {
                t.setBaseTerrain(BaseTerrainEnum::Water);
                break;
            }
        }
    }

    // 5) Pick capitals far apart on land (inside border 2..N-3)
    std::vector<int> candidates;
    candidates.reserve(N * N);
    for (int r = 2; r < N - 2; ++r) {
        for (int c = 2; c < N - 2; ++c) {
            int idx = r * N + c;
            if (map.allTiles()[static_cast<size_t>(idx)].getBaseTerrain() == BaseTerrainEnum::Land) {
                candidates.push_back(idx);
            }
        }
    }
    if (candidates.empty()) return;

    const size_t P = tribes.size();

    // Player order: keep overall order, but shuffle within groups of the same tribe type.
    std::vector<size_t> playerOrder(P);
    for (size_t i = 0; i < P; ++i) playerOrder[i] = i;

    {
        std::unordered_map<TribeType, std::vector<size_t>> groups;
        groups.reserve(P);
        for (size_t i = 0; i < P; ++i) {
            groups[tribes[i]].push_back(i);
        }
        for (auto& kv : groups) {
            auto& g = kv.second;
            if (g.size() > 1) {
                std::shuffle(g.begin(), g.end(), rng);
            }
        }

        std::unordered_map<TribeType, size_t> cursor;
        cursor.reserve(groups.size());
        for (size_t i = 0; i < P; ++i) cursor[tribes[i]] = 0;

        for (size_t i = 0; i < P; ++i) {
            TribeType tt = tribes[i];
            size_t& cur = cursor[tt];
            playerOrder[i] = groups[tt][cur++];
        }
    }

    std::vector<int> capitalCellsByPlayer(P, -1);
    std::vector<int> chosenCaps;
    chosenCaps.reserve(P);

    auto minDistToChosen = [&](int cell) -> int {
        if (chosenCaps.empty()) return N;
        int md = N;
        for (int cap : chosenCaps) {
            md = std::min(md, chebyshevDistance(cell, cap, N));
        }
        return md;
    };

    for (size_t k = 0; k < P; ++k) {
        const size_t playerIdx = playerOrder[k];

        int bestMinDist = -1;
        std::vector<int> bestCells;
        bestCells.reserve(64);

        for (int cell : candidates) {
            int md = minDistToChosen(cell);
            if (md > bestMinDist) {
                bestMinDist = md;
                bestCells.clear();
                bestCells.push_back(cell);
            } else if (md == bestMinDist) {
                bestCells.push_back(cell);
            }
        }

        int picked = bestCells[static_cast<size_t>(randInt(rng, 0, int(bestCells.size()) - 1))];

        capitalCellsByPlayer[playerIdx] = picked;
        chosenCaps.push_back(picked);

        Tile& t = map.allTiles()[static_cast<size_t>(picked)];
        t.setTribe(tribes[playerIdx]);
        t.setSettlement(SettlementTypeEnum::City, static_cast<SettlementId>(playerIdx));
    }

    s_lastCapitals.assign(P, Pos{});
    for (size_t i = 0; i < P; ++i) {
        int cell = capitalCellsByPlayer[i];
        s_lastCapitals[i] = Pos{ cell % N, cell / N };
    }

    std::vector<int> capitalCells;
    capitalCells.reserve(P);
    for (size_t i = 0; i < P; ++i) capitalCells.push_back(capitalCellsByPlayer[i]);

    // 6) Expand tribes territories (random frontier fill)
    std::vector<uint8_t> done(static_cast<size_t>(N * N), 0);
    std::vector<std::vector<int>> active(tribes.size());

    for (size_t i = 0; i < tribes.size(); ++i) {
        done[static_cast<size_t>(capitalCells[i])] = 1;
        active[i].push_back(capitalCells[i]);
    }

    int doneCount = int(tribes.size());
    while (doneCount < N * N) {
        bool progressed = false;

        for (size_t i = 0; i < tribes.size(); ++i) {
            if (active[i].empty()) continue;

            int pick = randInt(rng, 0, int(active[i].size()) - 1);
            int cell = active[i][static_cast<size_t>(pick)];

            auto neigh = circle(cell, 1, N);

            std::vector<int> valid;
            valid.reserve(neigh.size());
            for (int nb : neigh) {
                if (done[static_cast<size_t>(nb)]) continue;
                BaseTerrainEnum bt = map.allTiles()[static_cast<size_t>(nb)].getBaseTerrain();
                if (bt != BaseTerrainEnum::Water) valid.push_back(nb);
            }
            if (valid.empty()) {
                for (int nb : neigh) {
                    if (!done[static_cast<size_t>(nb)]) valid.push_back(nb);
                }
            }

            if (!valid.empty()) {
                int nb = valid[static_cast<size_t>(randInt(rng, 0, int(valid.size()) - 1))];
                map.allTiles()[static_cast<size_t>(nb)].setTribe(tribes[i]);
                done[static_cast<size_t>(nb)] = 1;
                doneCount++;
                active[i].push_back(nb);
                progressed = true;
            } else {
                active[i].erase(active[i].begin() + pick);
            }
        }

        if (!progressed) break;
    }

    // Safety: ensure every tile has a tribe assigned.
    // In rare cases the frontier expansion can stop early (e.g. due to empty actives),
    // leaving some tiles with TribeType::Unknown. Assign those to the nearest capital.
    {
        bool anyUnknown = false;
        for (int cell = 0; cell < N * N; ++cell) {
            if (map.allTiles()[static_cast<size_t>(cell)].getTribe() == TribeType::Unknown) {
                anyUnknown = true;
                break;
            }
        }

        if (anyUnknown && !capitalCells.empty()) {
            for (int cell = 0; cell < N * N; ++cell) {
                Tile& t = map.allTiles()[static_cast<size_t>(cell)];
                if (t.getTribe() != TribeType::Unknown) continue;

                int bestIdx = 0;
                int bestDist = chebyshevDistance(cell, capitalCells[0], N);
                for (int i = 1; i < (int)capitalCells.size(); ++i) {
                    int d = chebyshevDistance(cell, capitalCells[i], N);
                    if (d < bestDist) {
                        bestDist = d;
                        bestIdx = i;
                    }
                }

                t.setTribe(tribes[static_cast<size_t>(bestIdx)]);
            }
        }
    }

    // Water/Ocean should not belong to any tribe (keep Unknown)
    for (int cell = 0; cell < N * N; ++cell) {
        Tile& t = map.allTiles()[static_cast<size_t>(cell)];
        BaseTerrainEnum bt = t.getBaseTerrain();
        if (bt == BaseTerrainEnum::Water || bt == BaseTerrainEnum::Ocean) {
            t.setTribe(TribeType::Unknown);
        }
    }

    // 7) Add forests / mountains and occasional extra ocean (tribe-based)
    const float P_MOUNTAIN = 0.15f;
    const float P_FOREST   = 0.40f;

    for (int cell = 0; cell < N * N; ++cell) {
        Tile& t = map.allTiles()[static_cast<size_t>(cell)];
        if (t.getBaseTerrain() != BaseTerrainEnum::Land) continue;
        // Never turn a settlement tile (capitals, villages, ruins, starfish, etc.) into water/ocean.
        // This pass is only for shaping empty terrain.
        if (t.getSettlementType() != SettlementTypeEnum::None) continue;

        TribeProbs pr = probsForTribe(t.getTribe());

        float r = rand01(rng);
        if (r < P_FOREST * pr.forest) {
            t.setResource(ResourcesEnum::Forest);
        } else if (r > 1.0f - (P_MOUNTAIN * pr.mountain)) {
            t.setBaseTerrain(BaseTerrainEnum::Mountain);
        }

        float w = rand01(rng);
        if (w < pr.water) {
            t.setBaseTerrain(BaseTerrainEnum::Ocean);
            t.setResource(ResourcesEnum::None);
            t.setTribe(TribeType::Unknown);
        }
    }

    // 7b) Fix shorelines again after tribe-based ocean pass.
    // The biome pass above can turn some land into Ocean, creating Ocean tiles directly adjacent to Land.
    // We want the gradient: Ocean -> Water -> Land, so:
    //   - Any Ocean tile adjacent (4-neighborhood) to land-like becomes Water.
    //   - Any Water tile with no adjacent land-like becomes Ocean.
    {
        std::vector<uint8_t> toWater(static_cast<size_t>(N * N), 0);
        std::vector<uint8_t> toOcean(static_cast<size_t>(N * N), 0);

        // Ocean -> Water if it touches land.
        for (int cell = 0; cell < N * N; ++cell) {
            const Tile& t = map.allTiles()[static_cast<size_t>(cell)];
            if (t.getBaseTerrain() != BaseTerrainEnum::Ocean) continue;

            bool touchesLand = false;
            for (int nb : plusSign(cell, N)) {
                BaseTerrainEnum nt = map.allTiles()[static_cast<size_t>(nb)].getBaseTerrain();
                if (isLandLike(nt)) { touchesLand = true; break; }
            }
            if (touchesLand) toWater[static_cast<size_t>(cell)] = 1;
        }

        for (int cell = 0; cell < N * N; ++cell) {
            if (!toWater[static_cast<size_t>(cell)]) continue;
            map.allTiles()[static_cast<size_t>(cell)].setBaseTerrain(BaseTerrainEnum::Water);
        }

        // Water -> Ocean if it doesn't touch land anymore.
        for (int cell = 0; cell < N * N; ++cell) {
            const Tile& t = map.allTiles()[static_cast<size_t>(cell)];
            if (t.getBaseTerrain() != BaseTerrainEnum::Water) continue;

            bool touchesLand = false;
            for (int nb : plusSign(cell, N)) {
                BaseTerrainEnum nt = map.allTiles()[static_cast<size_t>(nb)].getBaseTerrain();
                if (isLandLike(nt)) { touchesLand = true; break; }
            }
            if (!touchesLand) toOcean[static_cast<size_t>(cell)] = 1;
        }

        for (int cell = 0; cell < N * N; ++cell) {
            if (!toOcean[static_cast<size_t>(cell)]) continue;
            map.allTiles()[static_cast<size_t>(cell)].setBaseTerrain(BaseTerrainEnum::Ocean);
        }
    }

    // 8) Village map (spacing like python)
    std::vector<int8_t> vmap(static_cast<size_t>(N * N), 0);
    for (int cell = 0; cell < N * N; ++cell) {
        int row = cell / N;
        int col = cell % N;

        BaseTerrainEnum bt = map.allTiles()[static_cast<size_t>(cell)].getBaseTerrain();
        if (bt == BaseTerrainEnum::Ocean || bt == BaseTerrainEnum::Mountain) {
            vmap[static_cast<size_t>(cell)] = -1;
        } else if (row == 0 || row == N - 1 || col == 0 || col == N - 1) {
            vmap[static_cast<size_t>(cell)] = -1;
        } else {
            vmap[static_cast<size_t>(cell)] = 0;
        }
    }

    for (int cap : capitalCells) {
        vmap[static_cast<size_t>(cap)] = 3;
        for (int c : circle(cap, 1, N)) vmap[static_cast<size_t>(c)] = std::max<int8_t>(vmap[static_cast<size_t>(c)], 2);
        for (int c : circle(cap, 2, N)) vmap[static_cast<size_t>(c)] = std::max<int8_t>(vmap[static_cast<size_t>(c)], 1);
    }

    auto proc = [&](int cell, float probability) -> bool {
        if (vmap[static_cast<size_t>(cell)] == 2) return rand01(rng) < probability;
        if (vmap[static_cast<size_t>(cell)] == 1) return rand01(rng) < probability * BORDER_EXPANSION;
        return false;
    };

    while (true) {
        std::vector<int> zeros;
        zeros.reserve(N * N);
        for (int i = 0; i < N * N; ++i) {
            if (vmap[static_cast<size_t>(i)] == 0) zeros.push_back(i);
        }
        if (zeros.empty()) break;

        int newVillage = zeros[static_cast<size_t>(randInt(rng, 0, int(zeros.size()) - 1))];
        vmap[static_cast<size_t>(newVillage)] = 3;
        for (int c : circle(newVillage, 1, N)) vmap[static_cast<size_t>(c)] = std::max<int8_t>(vmap[static_cast<size_t>(c)], 2);
        for (int c : circle(newVillage, 2, N)) vmap[static_cast<size_t>(c)] = std::max<int8_t>(vmap[static_cast<size_t>(c)], 1);
    }

    // Ensure that tiles selected for villages are not forests (village replaces trees).
    for (int cell = 0; cell < N * N; ++cell) {
        if (vmap[static_cast<size_t>(cell)] == 3) {
            Tile& t = map.allTiles()[static_cast<size_t>(cell)];
            if (t.getResource() == ResourcesEnum::Forest) {
                t.setResource(ResourcesEnum::None);
            }
        }
    }

    // 9) Resources + villages (above layer mapping)
    const float P_FRUIT = 0.50f;
    const float P_CROPS = 0.50f;
    const float P_ANIM  = 0.50f;
    const float P_FISH  = 0.50f;
    const float P_METAL = 0.50f;

    SettlementId nextSettlementId = static_cast<SettlementId>(tribes.size()); // capitals used 0..tribes-1

    for (int cell = 0; cell < N * N; ++cell) {
        Tile& t = map.allTiles()[static_cast<size_t>(cell)];
        TribeProbs pr = probsForTribe(t.getTribe());

        if (t.getSettlementType() == SettlementTypeEnum::City) continue; // capital already

        BaseTerrainEnum bt = t.getBaseTerrain();
        if (bt == BaseTerrainEnum::Land) {
            float fruit = P_FRUIT * pr.fruit;
            float crops = P_CROPS * pr.crops;

            if (vmap[static_cast<size_t>(cell)] == 3) {
                // Villages must not be generated on forest tiles.
                // If forest was generated earlier (biome pass), clear it here.
                if (t.getResource() == ResourcesEnum::Forest) {
                    t.setResource(ResourcesEnum::None);
                }
                t.setSettlement(SettlementTypeEnum::Village, nextSettlementId++);
            } else if (proc(cell, fruit * (1.0f - crops / 2.0f))) {
                t.setResource(ResourcesEnum::Fruit);
            } else if (proc(cell, crops * (1.0f - fruit / 2.0f))) {
                t.setResource(ResourcesEnum::Crops);
            } else if (proc(cell, P_ANIM * pr.animals)) {
                t.setResource(ResourcesEnum::Animals);
            }
        } else if (bt == BaseTerrainEnum::Water) {
            if (proc(cell, P_FISH * pr.fish)) {
                t.setResource(ResourcesEnum::Fish);
            }
        } else if (bt == BaseTerrainEnum::Ocean) {
            // Starfish are placed in a dedicated pass below (global frequency rules)
        } else if (bt == BaseTerrainEnum::Mountain) {
            if (proc(cell, P_METAL * pr.metal)) {
                t.setResource(ResourcesEnum::Metal);
            }
        }
    }

    // 9b) Starfish: approximately 1 starfish per 25 water tiles.
    // Rules (wiki): may spawn on Water or Ocean; cannot be adjacent to another starfish, lighthouse, or city.
    // NOTE: In this project Starfish is a SettlementType (not a Resource), so we place it via setSettlement().
    {
        int waterTiles = 0;
        for (int cell = 0; cell < N * N; ++cell) {
            BaseTerrainEnum bt = map.allTiles()[static_cast<size_t>(cell)].getBaseTerrain();
            if (bt == BaseTerrainEnum::Water || bt == BaseTerrainEnum::Ocean) waterTiles++;
        }

        const int targetStarfish = waterTiles / 25;
        if (targetStarfish > 0) {
            std::vector<int> candidatesSF;
            candidatesSF.reserve(static_cast<size_t>(waterTiles));

            auto isCityOrLighthouse = [&](int cell) -> bool {
                const Tile& t = map.allTiles()[static_cast<size_t>(cell)];
                if (t.getSettlementType() == SettlementTypeEnum::City) return true;
                // requires BuildingTypeEnum::Lighthouse to exist
                if (t.getBuildingType() == BuildingTypeEnum::Lighthouse) return true;
                return false;
            };

            auto hasAdjacentStarfish = [&](int cell) -> bool {
                for (int nb : circle(cell, 1, N)) {
                    if (nb == cell) continue;
                    const Tile& nt = map.allTiles()[static_cast<size_t>(nb)];
                    if (nt.getSettlementType() == SettlementTypeEnum::Starfish) return true;
                }
                return false;
            };

            for (int cell = 0; cell < N * N; ++cell) {
                const Tile& t = map.allTiles()[static_cast<size_t>(cell)];
                BaseTerrainEnum bt = t.getBaseTerrain();
                if (!(bt == BaseTerrainEnum::Water || bt == BaseTerrainEnum::Ocean)) continue;

                // Must be an empty water tile (no other resource, settlement, or building).
                if (t.getResource() != ResourcesEnum::None) continue;
                if (t.getSettlementType() != SettlementTypeEnum::None) continue;
                if (t.getBuildingType() != BuildingTypeEnum::None) continue;

                bool blocked = false;
                for (int nb : circle(cell, 1, N)) {
                    if (nb == cell) continue;
                    if (isCityOrLighthouse(nb)) { blocked = true; break; }
                }
                if (blocked) continue;
                if (hasAdjacentStarfish(cell)) continue;

                candidatesSF.push_back(cell);
            }

            std::shuffle(candidatesSF.begin(), candidatesSF.end(), rng);

            int placedSF = 0;
            for (int cell : candidatesSF) {
                if (placedSF >= targetStarfish) break;

                if (hasAdjacentStarfish(cell)) continue;

                bool blocked = false;
                for (int nb : circle(cell, 1, N)) {
                    if (nb == cell) continue;
                    if (isCityOrLighthouse(nb)) { blocked = true; break; }
                }
                if (blocked) continue;

                // Allocate a unique settlement id for each starfish (same approach as villages/ruins).
                map.allTiles()[static_cast<size_t>(cell)].setSettlement(SettlementTypeEnum::Starfish, nextSettlementId++);
                placedSF++;
            }
        }
    }

    // 10) Ruins (roughly like python: N*N/40, 1/3 in water)
    int ruinsNumber = int(std::round(float(N * N) / 40.0f));
    int waterRuinsTarget = int(std::round(float(ruinsNumber) / 3.0f));
    int ruins = 0;
    int waterRuins = 0;

    std::vector<int> ruinCandidates;
    ruinCandidates.reserve(N * N);
    for (int cell = 0; cell < N * N; ++cell) {
        if (vmap[static_cast<size_t>(cell)] == 3) continue;
        if (vmap[static_cast<size_t>(cell)] == -1 || vmap[static_cast<size_t>(cell)] == 0 || vmap[static_cast<size_t>(cell)] == 1) {
            ruinCandidates.push_back(cell);
        }
    }

    while (ruins < ruinsNumber && !ruinCandidates.empty()) {
        int pick = ruinCandidates[static_cast<size_t>(randInt(rng, 0, int(ruinCandidates.size()) - 1))];
        Tile& t = map.allTiles()[static_cast<size_t>(pick)];

        if (t.getSettlementType() != SettlementTypeEnum::None) {
            ruinCandidates.erase(std::remove(ruinCandidates.begin(), ruinCandidates.end(), pick), ruinCandidates.end());
            continue;
        }

        bool inWater = (t.getBaseTerrain() == BaseTerrainEnum::Ocean || t.getBaseTerrain() == BaseTerrainEnum::Water);
        if (inWater && waterRuins >= waterRuinsTarget) {
            ruinCandidates.erase(std::remove(ruinCandidates.begin(), ruinCandidates.end(), pick), ruinCandidates.end());
            continue;
        }

        t.setSettlement(SettlementTypeEnum::Ruin, nextSettlementId++);
        if (inWater) waterRuins++;
        ruins++;

        for (int c : circle(pick, 1, N)) {
            if (vmap[static_cast<size_t>(c)] >= 0) vmap[static_cast<size_t>(c)] = std::max<int8_t>(vmap[static_cast<size_t>(c)], 2);
        }
    }
}

const std::vector<Pos>& MapGenerator::getLastCapitals() {
    return s_lastCapitals;
}