//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_MOVEMENTSYSTEM_H
#define GAME_ENGINE_MOVEMENTSYSTEM_H

#include <vector>
#include <cstdint>

#include "World/Pos.h"
#include "units/Unit.h" // UnitId

class Game;

// Movement rules live here (Unit remains data-only).
// This initial version supports square-grid movement with 4-neighbour steps (N/E/S/W)
// and a maximum path length equal to Unit::movePoints.
// Terrain/skills (Amphibious, roads, etc.) can be layered on later.
class MovementSystem {
public:
    // Try to move unit to destination tile.
    // Returns true on success (state is updated: map occupancy, unit pos, move points, moved flag).
    static bool move(Game& game, UnitId unitId, Pos to);

    // Compute reachable tiles within current move points (does not mutate game).
    static std::vector<Pos> reachable(const Game& game, UnitId unitId);

private:
    // Shortest path distance on 4-neighbour grid with obstacles (occupied tiles are blocked except the start).
    // Returns -1 if unreachable.
    static int shortestPathDistance(const Game& game, Pos from, Pos to);
};

#endif //GAME_ENGINE_MOVEMENTSYSTEM_H