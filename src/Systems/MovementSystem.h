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

class MovementSystem {
public:
    static bool move(Game& game, UnitId unitId, Pos to);
    static std::vector<Pos> reachable(const Game& game, UnitId unitId);

private:
    static int shortestPathDistance(const Game& game, Pos from, Pos to);
};

#endif //GAME_ENGINE_MOVEMENTSYSTEM_H