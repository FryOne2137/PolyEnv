//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_MAPGENERATOR_H
#define GAME_ENGINE_MAPGENERATOR_H

#include <cstdint>
#include <random>
#include <vector>

#include "world/Map.h"
#include "../content/tribes/Tribe.h"
#include "world/Pos.h"

class MapGenerator {
public:
    struct Params {
        int mapSize = 16;          // width = height
        float initialLand = 0.5f;  // [0..1]
        int smoothing = 3;         // cellular automata iterations
        int relief = 4;            // affects land/water threshold like in the python code
        uint32_t seed = 0;         // 0 -> random_device
    };

    // Generates a Polytopia-like map into `map`.
    // - `map` will be re-initialized to params.mapSize x params.mapSize if needed.
    // - `map.getActiveTribes()` must contain 2..16 tribes.
    static void generate(Map& map, const Params& params);
    static const std::vector<Pos>& getLastCapitals();

private:
    static std::vector<Pos> s_lastCapitals;
    // Grid helpers (square grid, Chebyshev distance like in the python generator)
    static int chebyshevDistance(int a, int b, int size);
    static std::vector<int> circle(int center, int radius, int size);
    static std::vector<int> round(int center, int radius, int size);
    static std::vector<int> plusSign(int center, int size);

    static int randInt(std::mt19937& rng, int a, int b);
    static float rand01(std::mt19937& rng);

    static bool isLandLike(BaseTerrainEnum t);

    struct TribeProbs {
        float water = 0.0f;
        float forest = 1.0f;
        float mountain = 1.0f;
        float metal = 1.0f;
        float fruit = 1.0f;
        float crops = 1.0f;
        float animals = 1.0f;
        float fish = 1.0f;
        float starfish = 1.0f;
    };

    static TribeProbs probsForTribe(TribeType tribe);
};

#endif //GAME_ENGINE_MAPGENERATOR_H