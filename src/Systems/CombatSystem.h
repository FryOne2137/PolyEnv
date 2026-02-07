//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_COMBATSYSTEM_H
#define GAME_ENGINE_COMBATSYSTEM_H

#include "Game.h"

class CombatSystem {
public:

    static bool attack(Game& game, UnitId attackerId, Pos targetPos);
    static bool heal(Game& game, UnitId healerId);
    static std::vector<Pos> attackable(const Game& game, UnitId attackerId);


private:
    static int chebyshevDistance(Pos a, Pos b);

};

#endif //GAME_ENGINE_COMBATSYSTEM_H