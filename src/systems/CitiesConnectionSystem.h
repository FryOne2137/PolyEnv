//
// Created by Fryderyk Niedzwiecki on 02/02/2026.
//

#ifndef GAME_ENGINE_CITIESCONNECTIONSYSTEM_H
#define GAME_ENGINE_CITIESCONNECTIONSYSTEM_H

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <vector>

class Game;

// Stateful connection data. It belongs to one Game and is therefore copied
// with Game clones used by MCTS.
struct CitiesConnectionRuntime {
    std::unordered_map<int, std::unordered_set<int>> connected;
    std::unordered_map<int, int> previousCapital;
    std::unordered_map<int, std::vector<int>> waterConnectionTiles;
    std::vector<uint32_t> stamp;
    uint32_t epoch = 1;
    std::vector<int> queue;
    std::vector<uint8_t> waterOwner;
    std::vector<int> parent;
    std::vector<uint16_t> distance;
    std::vector<int> bfsQueue;
};

// Polytopia-like city connections:
// - Roads and Bridges connect over land.
// - Ports connect to other ports via contiguous water tiles.
// Each connected city grants +1 population to both the capital and that city;
// if the connection is later lost, that population is removed.
class CitiesConnectionSystem {
public:
    static void update(Game& game);
};

#endif //GAME_ENGINE_CITIESCONNECTIONSYSTEM_H
