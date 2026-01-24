//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_COMBATSYSTEM_H
#define GAME_ENGINE_COMBATSYSTEM_H

#include "Game.h"

class CombatSystem {
public:
    static bool attack(Game& game, UnitId attackerId, Pos targetPos);
};

#endif //GAME_ENGINE_COMBATSYSTEM_H