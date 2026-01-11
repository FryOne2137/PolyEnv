//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

// generateMap.cpp
#include "generateMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

// ---------- Utils ----------
std::vector<int> circle(int center, int radius, int mapSize) {
    std::vector<int> out;
    out.reserve(std::max(0, 8 * radius));

    const int row = center / mapSize;
    const int col = center % mapSize;

    int i = row - radius;
    if (0 <= i && i < mapSize) {
        for (int j = col - radius; j < col + radius; ++j) {
            if (0 <= j && j < mapSize) out.push_back(i * mapSize + j);
        }
    }

    i = row + radius;
    if (0 <= i && i < mapSize) {
        for (int j = col + radius; j > col - radius; --j) {
            if (0 <= j && j < mapSize) out.push_back(i * mapSize + j);
        }
    }

    int j = col - radius;
    if (0 <= j && j < mapSize) {
        for (int ii = row + radius; ii > row - radius; --ii) {
            if (0 <= ii && ii < mapSize) out.push_back(ii * mapSize + j);
        }
    }

    j = col + radius;
    if (0 <= j && j < mapSize) {
        for (int ii = row - radius; ii < row + radius; ++ii) {
            if (0 <= ii && ii < mapSize) out.push_back(ii * mapSize + j);
        }
    }

    return out;
}

std::vector<int> round_(int center, int radius, int mapSize) {
    std::vector<int> out;
    for (int r = 1; r <= radius; ++r) {
        auto c = circle(center, r, mapSize);
        out.insert(out.end(), c.begin(), c.end());
    }
    out.push_back(center);
    return out;
}

int distanceChebyshev(int a, int b, int size) {
    const int ax = a % size;
    const int ay = a / size;
    const int bx = b % size;
    const int by = b / size;
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

std::vector<int> plusSign(int center, int mapSize) {
    std::vector<int> out;
    out.reserve(4);

    const int row = center / mapSize;
    const int col = center % mapSize;

    if (col > 0) out.push_back(center - 1);
    if (col < mapSize - 1) out.push_back(center + 1);
    if (row > 0) out.push_back(center - mapSize);
    if (row < mapSize - 1) out.push_back(center + mapSize);

    return out;
}

// ---------- Generator ----------
static constexpr double BORDER_EXPANSION = 1.0 / 3.0;

static int pickRandomIndex(std::mt19937& rng, int n) {
    std::uniform_int_distribution<int> dist(0, n - 1);
    return dist(rng);
}

static double rand01(std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

static int checkResources(const std::vector<Tile>& world, int capital, int mapSize, const std::string& resource) {
    int count = 0;
    auto neigh = circle(capital, 1, mapSize);
    for (int n : neigh) {
        if (world[n].above == resource) ++count;
    }
    return count;
}

static void postGenerate(
    std::vector<Tile>& world,
    int capital,
    int mapSize,
    const std::string& resource,
    const std::string& underneath,
    int quantity
) {
    int resources = checkResources(world, capital, mapSize, resource);
    while (resources < quantity) {
        const auto territory = circle(capital, 1, mapSize);
        if (territory.empty()) break;

        // python: pos_ = random.randrange(0, 8) (zakłada 8 pól w "ring")
        // tu: bezpiecznie losujemy po realnym rozmiarze
        std::random_device rd;
        std::mt19937 localRng(rd());
        int pos = pickRandomIndex(localRng, (int)territory.size());

        int t = territory[pos];
        world[t].type = underneath;
        world[t].above = resource;

        // jeżeli obok ocean -> water (jak python)
        for (int nb : plusSign(t, mapSize)) {
            if (world[nb].type == "ocean") {
                world[nb].type = "water";
            }
        }

        resources = checkResources(world, capital, mapSize, resource);
    }
}

GeneratedMap generateMap(const MapConfig& cfg) {
    if (cfg.mapSize <= 0) throw std::invalid_argument("mapSize must be > 0");
    if (cfg.initialLand < 0.0 || cfg.initialLand > 1.0) throw std::invalid_argument("initialLand must be in [0,1]");
    if (cfg.smoothing < 0) throw std::invalid_argument("smoothing must be >= 0");
    if (cfg.tribes.empty()) throw std::invalid_argument("tribes cannot be empty");

    std::mt19937 rng;
    if (cfg.useSeed) rng.seed(cfg.seed);
    else rng.seed(std::random_device{}());

    const int mapSize = cfg.mapSize;
    const int N = mapSize * mapSize;

    // --- Stałe z pythona (te "kreski") ---
    const double _____ = 2.0;
    const double ____  = 1.5;
    const double ___   = 1.0;
    const double __    = 0.5;
    const double _     = 0.1;

    // terrain_probs[terrain][tribe]
    std::unordered_map<std::string, std::unordered_map<std::string, double>> terrain_probs;
    terrain_probs["water"] = {
        {"Xin-xi", 0}, {"Imperius", 0}, {"Bardur", 0}, {"Oumaji", 0}, {"Kickoo", 0.4},
        {"Hoodrick", 0}, {"Luxidoor", 0}, {"Vengir", 0}, {"Zebasi", 0}, {"Ai-mo", 0},
        {"Quetzali", 0}, {"Yadakk", 0}, {"Aquarion", 0.3}, {"Elyrion", 0}, {"Polaris", 0}
    };
    terrain_probs["forest"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", ___}, {"Oumaji", _}, {"Kickoo", ___},
        {"Hoodrick", ____}, {"Luxidoor", ___}, {"Vengir", ___}, {"Zebasi", __}, {"Ai-mo", ___},
        {"Quetzali", ___}, {"Yadakk", __}, {"Aquarion", __}, {"Elyrion", ___}, {"Polaris", ___}
    };
    terrain_probs["mountain"] = {
        {"Xin-xi", ____}, {"Imperius", ___}, {"Bardur", ___}, {"Oumaji", ___}, {"Kickoo", __},
        {"Hoodrick", __}, {"Luxidoor", ___}, {"Vengir", ___}, {"Zebasi", __}, {"Ai-mo", ____},
        {"Quetzali", ___}, {"Yadakk", __}, {"Aquarion", ___}, {"Elyrion", __}, {"Polaris", ___}
    };
    terrain_probs["metal"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", ___}, {"Oumaji", ___}, {"Kickoo", ___},
        {"Hoodrick", ___}, {"Luxidoor", ___}, {"Vengir", _____}, {"Zebasi", ___}, {"Ai-mo", ___},
        {"Quetzali", _}, {"Yadakk", ___}, {"Aquarion", ___}, {"Elyrion", ___}, {"Polaris", ___}
    };
    terrain_probs["fruit"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", ____}, {"Oumaji", ___}, {"Kickoo", ___},
        {"Hoodrick", ___}, {"Luxidoor", _____}, {"Vengir", _}, {"Zebasi", __}, {"Ai-mo", ___},
        {"Quetzali", _____}, {"Yadakk", ____}, {"Aquarion", ___}, {"Elyrion", ___}, {"Polaris", ___}
    };
    terrain_probs["crop"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", _}, {"Oumaji", ___}, {"Kickoo", ___},
        {"Hoodrick", ___}, {"Luxidoor", ___}, {"Vengir", ___}, {"Zebasi", ___}, {"Ai-mo", _},
        {"Quetzali", _}, {"Yadakk", ___}, {"Aquarion", ___}, {"Elyrion", ____}, {"Polaris", ___}
    };
    terrain_probs["game"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", _____}, {"Oumaji", ___}, {"Kickoo", ___},
        {"Hoodrick", ___}, {"Luxidoor", __}, {"Vengir", _}, {"Zebasi", ___}, {"Ai-mo", ___},
        {"Quetzali", ___}, {"Yadakk", ___}, {"Aquarion", ___}, {"Elyrion", ___}, {"Polaris", ___}
    };
    terrain_probs["fish"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", ___}, {"Oumaji", ___}, {"Kickoo", ____},
        {"Hoodrick", ___}, {"Luxidoor", ___}, {"Vengir", _}, {"Zebasi", ___}, {"Ai-mo", ___},
        {"Quetzali", ___}, {"Yadakk", ___}, {"Aquarion", ___}, {"Elyrion", ___}, {"Polaris", ___}
    };
    terrain_probs["whale"] = {
        {"Xin-xi", ___}, {"Imperius", ___}, {"Bardur", ___}, {"Oumaji", ___}, {"Kickoo", ___},
        {"Hoodrick", ___}, {"Luxidoor", ___}, {"Vengir", ___}, {"Zebasi", ___}, {"Ai-mo", ___},
        {"Quetzali", ___}, {"Yadakk", ___}, {"Aquarion", ___}, {"Elyrion", ___}, {"Polaris", ___}
    };

    std::unordered_map<std::string, double> general_probs = {
        {"mountain", 0.15}, {"forest", 0.4}, {"fruit", 0.5}, {"crop", 0.5},
        {"fish", 0.5}, {"game", 0.5}, {"whale", 0}, {"metal", 0.5}
    };

    // --- world_map init ---
    std::vector<Tile> world(N);
    for (int i = 0; i < N; ++i) {
        world[i].type = "ocean";
        world[i].above = "";
        world[i].road = false;
        world[i].tribe = "Xin-xi";
    }

    // --- initial land seeds ---
    int j = 0;
    const int targetLand = (int)std::floor(N * cfg.initialLand);
    while (j < targetLand) {
        int cell = pickRandomIndex(rng, N);
        if (world[cell].type == "ocean") {
            ++j;
            world[cell].type = "ground";
        }
    }

    // --- smoothing ---
    const double land_coefficient = (0.5 + (double)cfg.relief) / 9.0;
    for (int s = 0; s < cfg.smoothing; ++s) {
        for (int cell = 0; cell < N; ++cell) {
            int waterCount = 0;
            int tileCount = 0;
            auto neighbours = round_(cell, 1, mapSize);
            for (int nb : neighbours) {
                if (world[nb].type == "ocean") ++waterCount;
                ++tileCount;
            }
            if (tileCount > 0 && (double)waterCount / (double)tileCount <= land_coefficient) {
                world[cell].road = true;
            }
        }
        for (int cell = 0; cell < N; ++cell) {
            if (world[cell].road) {
                world[cell].road = false;
                world[cell].type = "ground";
            } else {
                world[cell].type = "ocean";
            }
        }
    }

    // --- capitals placement ---
    std::vector<int> capitalCells;
    std::unordered_map<int, int> capitalMap; // cell -> score

    for (int row = 2; row < mapSize - 2; ++row) {
        for (int col = 2; col < mapSize - 2; ++col) {
            int idx = row * mapSize + col;
            if (world[idx].type == "ground") capitalMap[idx] = 0;
        }
    }

    for (size_t t = 0; t < cfg.tribes.size(); ++t) {
        int maxScore = 0;

        for (auto& kv : capitalMap) {
            int cell = kv.first;
            int best = mapSize;
            for (int cap : capitalCells) {
                best = std::min(best, distanceChebyshev(cell, cap, mapSize));
            }
            kv.second = best;
            maxScore = std::max(maxScore, best);
        }

        int countMax = 0;
        for (const auto& kv : capitalMap) if (kv.second == maxScore) ++countMax;
        if (countMax == 0) break;

        int pick = pickRandomIndex(rng, countMax);
        for (const auto& kv : capitalMap) {
            if (kv.second == maxScore) {
                if (pick == 0) {
                    capitalCells.push_back(kv.first);
                    break;
                }
                --pick;
            }
        }
    }

    for (size_t i = 0; i < capitalCells.size() && i < cfg.tribes.size(); ++i) {
        int c = capitalCells[i];
        world[c].above = "capital";
        world[c].tribe = cfg.tribes[i];
    }

    // --- tribe expansion ---
    std::vector<int> doneTiles;
    doneTiles.reserve(N);
    std::vector<std::vector<int>> activeTiles(cfg.tribes.size());

    for (size_t i = 0; i < capitalCells.size() && i < cfg.tribes.size(); ++i) {
        doneTiles.push_back(capitalCells[i]);
        activeTiles[i].push_back(capitalCells[i]);
    }

    auto isDone = [&](int cell) -> bool {
        return std::find(doneTiles.begin(), doneTiles.end(), cell) != doneTiles.end();
    };

    while ((int)doneTiles.size() != N) {
        for (size_t i = 0; i < cfg.tribes.size(); ++i) {
            if (!activeTiles[i].empty() && cfg.tribes[i] != "Polaris") {
                int randIdx = pickRandomIndex(rng, (int)activeTiles[i].size());
                int randCell = activeTiles[i][randIdx];

                auto neigh = circle(randCell, 1, mapSize);

                // python: world_map[tile]['type'] != 'water'
                // UWAGA: w pythonie na tym etapie praktycznie nie ma "water" (jest "ocean"/"ground"),
                // ale trzymamy logikę 1:1.
                std::vector<int> valid;
                valid.reserve(neigh.size());
                for (int n : neigh) {
                    if (!isDone(n) && world[n].type != "water") valid.push_back(n);
                }
                if (valid.empty()) {
                    for (int n : neigh) if (!isDone(n)) valid.push_back(n);
                }

                if (!valid.empty()) {
                    int newCell = valid[pickRandomIndex(rng, (int)valid.size())];
                    world[newCell].tribe = cfg.tribes[i];
                    activeTiles[i].push_back(newCell);
                    doneTiles.push_back(newCell);
                } else {
                    activeTiles[i].erase(activeTiles[i].begin() + randIdx);
                }
            }
        }
    }

    // --- terrain on ground tiles ---
    for (int cell = 0; cell < N; ++cell) {
        if (world[cell].type == "ground" && world[cell].above.empty()) {
            double r = rand01(rng);
            const std::string& tr = world[cell].tribe;

            if (r < general_probs["forest"] * terrain_probs["forest"][tr]) {
                world[cell].type = "forest";
            } else if (r > 1.0 - general_probs["mountain"] * terrain_probs["mountain"][tr]) {
                world[cell].type = "mountain";
            }

            r = rand01(rng);
            if (r < terrain_probs["water"][tr]) {
                world[cell].type = "ocean";
            }
        }
    }

    // --- village map init ---
    std::vector<int> villageMap;
    villageMap.reserve(N);

    for (int cell = 0; cell < N; ++cell) {
        int row = cell / mapSize;
        int col = cell % mapSize;

        if (world[cell].type == "ocean" || world[cell].type == "mountain") {
            villageMap.push_back(-1);
        } else if (row == 0 || row == mapSize - 1 || col == 0 || col == mapSize - 1) {
            villageMap.push_back(-1);
        } else {
            villageMap.push_back(0);
        }
    }

    // --- convert ocean near land into water ---
    const std::vector<std::string> landLike = {"ground", "forest", "mountain"};
    auto isLandLike = [&](const std::string& t) {
        return std::find(landLike.begin(), landLike.end(), t) != landLike.end();
    };

    for (int cell = 0; cell < N; ++cell) {
        if (world[cell].type == "ocean") {
            for (int nb : plusSign(cell, mapSize)) {
                if (isLandLike(world[nb].type)) {
                    world[cell].type = "water";
                    break;
                }
            }
        }
    }

    // --- capitals influence on village map ---
    for (int cap : capitalCells) {
        villageMap[cap] = 3;
        for (int c : circle(cap, 1, mapSize)) villageMap[c] = std::max(villageMap[c], 2);
        for (int c : circle(cap, 2, mapSize)) villageMap[c] = std::max(villageMap[c], 1);
    }

    int villageCount = 0;
    while (std::find(villageMap.begin(), villageMap.end(), 0) != villageMap.end()) {
        std::vector<int> zeros;
        zeros.reserve(N);
        for (int i = 0; i < N; ++i) if (villageMap[i] == 0) zeros.push_back(i);
        if (zeros.empty()) break;

        int newVillage = zeros[pickRandomIndex(rng, (int)zeros.size())];
        villageMap[newVillage] = 3;

        for (int c : circle(newVillage, 1, mapSize)) villageMap[c] = std::max(villageMap[c], 2);
        for (int c : circle(newVillage, 2, mapSize)) villageMap[c] = std::max(villageMap[c], 1);

        ++villageCount;
    }

    auto proc = [&](int cell, double probability) -> bool {
        if (villageMap[cell] == 2) return rand01(rng) < probability;
        if (villageMap[cell] == 1) return rand01(rng) < probability * BORDER_EXPANSION;
        return false;
    };

    // --- resources / villages placement ---
    for (int cell = 0; cell < N; ++cell) {
        const std::string& tr = world[cell].tribe;

        if (world[cell].type == "ground") {
            double fruit = general_probs["fruit"] * terrain_probs["fruit"][tr];
            double crop  = general_probs["crop"]  * terrain_probs["crop"][tr];

            if (world[cell].above != "capital") {
                if (villageMap[cell] == 3) {
                    world[cell].above = "village";
                } else if (proc(cell, fruit * (1.0 - crop / 2.0))) {
                    world[cell].above = "fruit";
                } else if (proc(cell, crop * (1.0 - fruit / 2.0))) {
                    world[cell].above = "crop";
                }
            }
        } else if (world[cell].type == "forest") {
            if (world[cell].above != "capital") {
                if (villageMap[cell] == 3) {
                    world[cell].type = "ground";
                    world[cell].above = "village";
                } else if (proc(cell, general_probs["game"] * terrain_probs["game"][tr])) {
                    world[cell].above = "game";
                }
            }
        } else if (world[cell].type == "water") {
            if (proc(cell, general_probs["fish"] * terrain_probs["fish"][tr])) {
                world[cell].above = "fish";
            }
        } else if (world[cell].type == "ocean") {
            if (proc(cell, general_probs["whale"] * terrain_probs["whale"][tr])) {
                world[cell].above = "whale";
            }
        } else if (world[cell].type == "mountain") {
            if (proc(cell, general_probs["metal"] * terrain_probs["metal"][tr])) {
                world[cell].above = "metal";
            }
        }
    }

    // --- ruins ---
    const int ruinsNumber = (int)std::llround((double)N / 40.0);
    const int waterRuinsNumber = (int)std::llround((double)ruinsNumber / 3.0);

    int ruinsCount = 0;
    int waterRuinsCount = 0;

    while (ruinsCount < ruinsNumber) {
        std::vector<int> candidates;
        candidates.reserve(N);
        for (int i = 0; i < N; ++i) {
            int vm = villageMap[i];
            if (vm == -1 || vm == 0 || vm == 1) candidates.push_back(i);
        }
        if (candidates.empty()) break;

        int ruin = candidates[pickRandomIndex(rng, (int)candidates.size())];
        const std::string terrain = world[ruin].type;

        if (terrain != "water" && (waterRuinsCount < waterRuinsNumber || terrain != "ocean")) {
            world[ruin].above = "ruin";
            if (terrain == "ocean") ++waterRuinsCount;

            for (int c : circle(ruin, 1, mapSize)) villageMap[c] = std::max(villageMap[c], 2);
            ++ruinsCount;
        }
    }

    // --- post-generate perks per tribe (jak python) ---
    for (int cap : capitalCells) {
        const std::string& tr = world[cap].tribe;

        if (tr == "Imperius") {
            postGenerate(world, cap, mapSize, "fruit", "ground", 2);
        } else if (tr == "Bardur") {
            postGenerate(world, cap, mapSize, "game", "forest", 2);
        } else if (tr == "Kickoo") {
            int resources = checkResources(world, cap, mapSize, "fish");

            while (resources < 2) {
                auto territory = plusSign(cap, mapSize);
                if (territory.empty()) break;

                int pos = pickRandomIndex(rng, std::min(4, (int)territory.size()));
                int t = territory[pos];

                world[t].type = "water";
                world[t].above = "fish";

                for (int nb : plusSign(t, mapSize)) {
                    if (world[nb].type == "water") {
                        world[nb].type = "ocean";
                        for (int dnb : plusSign(nb, mapSize)) {
                            if (world[dnb].type != "water" && world[dnb].type != "ocean") {
                                world[nb].type = "water";
                                break;
                            }
                        }
                    }
                }

                resources = checkResources(world, cap, mapSize, "fish");
            }
            break;
        } else if (tr == "Zebasi") {
            postGenerate(world, cap, mapSize, "crop", "ground", 1);
        } else if (tr == "Elyrion") {
            postGenerate(world, cap, mapSize, "game", "forest", 2);
        } else if (tr == "Polaris") {
            for (int nb : circle(cap, 1, mapSize)) world[nb].tribe = "Polaris";
        }
    }

    GeneratedMap out;
    out.mapSize = mapSize;
    out.world = std::move(world);
    out.villageMap = std::move(villageMap);
    out.capitalCells = std::move(capitalCells);
    return out;
}