//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_INTERACTIONSYSTEM_H
#define GAME_ENGINE_INTERACTIONSYSTEM_H

#include "World/Pos.h"
#include "units/Unit.h" // UnitId

class Game;

class InteractionSystem {
public:
    static void onUnitEnteredTile(Game& game, UnitId unitId, Pos pos);
    static void handleCityCapture(Game& game, UnitId unitId, Pos pos);
    static void handleVillage(Game& game, UnitId unitId, Pos pos);



private:
    static void handleStarfish(Game& game, UnitId unitId, Pos pos);
    static void handleRuin(Game& game, UnitId unitId, Pos pos);
};

#endif //GAME_ENGINE_INTERACTIONSYSTEM_H