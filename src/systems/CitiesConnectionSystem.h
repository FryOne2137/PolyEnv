//
// Created by Fryderyk Niedzwiecki on 02/02/2026.
//

#ifndef GAME_ENGINE_CITIESCONNECTIONSYSTEM_H
#define GAME_ENGINE_CITIESCONNECTIONSYSTEM_H

#include <unordered_map>
#include <unordered_set>

class Game;

// Polytopia-like city connections:
// - Roads and Bridges connect over land.
// - Ports connect to other ports via contiguous water tiles.
// Each connected city grants +1 population to both the capital and that city;
// if the connection is later lost, that population is removed.
class CitiesConnectionSystem {
public:
    static void update(Game& game);

private:
    // Per-player: set of CityIds connected to the capital (excluding the capital).
    // Stored as int to keep this header independent of project typedef headers.
    std::unordered_map<int, std::unordered_set<int>> m_connected;
};

#endif //GAME_ENGINE_CITIESCONNECTIONSYSTEM_H