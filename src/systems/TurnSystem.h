//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_TURNSYSTEM_H
#define GAME_ENGINE_TURNSYSTEM_H

#include <cstdint>
#include "core/Ids.h"

class Game;

class TurnSystem {
public:
    // Called at the beginning of current player's turn.
    // - resets unit turn flags (moved/attacked)
    // - refreshes movement points (jeśli trzymasz "maxMove" osobno, to tu ustawiasz na max)
    // - applies start-of-turn income (stars) etc.
    static void startTurn(Game &game);

    // Called when current player ends their turn.
    // - you can apply end-of-turn effects here (poison ticks, healing, etc.)
    static void endTurn(Game &game);

    // Hook for UI + income application (can be 0 until city economy is implemented)
    static int calcIncomeForPlayer(const Game& game, PlayerId pid);

private:
    static void applyPassiveRecoveryForCurrentPlayer(Game& game);
    static void refreshUnitsForCurrentPlayer(Game& game);
    static void applyIncomeForCurrentPlayer(Game &game, PlayerId pid);
};

#endif // GAME_ENGINE_TURNSYSTEM_H
