//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_MOVEMENTSYSTEM_H
#define GAME_ENGINE_MOVEMENTSYSTEM_H

#include <vector>
#include <cstdint>

#include "world/Pos.h"
#include "../content/units/Unit.h"

class Game;

class MovementSystem {
public:
    static bool move(Game& game, UnitId unitId, Pos to);
    static std::vector<Pos> reachable(const Game& game, UnitId unitId);
    static bool forceMove(Game& game, UnitId pushedUnit, Pos spawnPos);

private:
    static int shortestPathDistance(const Game& game, Pos from, Pos to);
};

#endif //GAME_ENGINE_MOVEMENTSYSTEM_H