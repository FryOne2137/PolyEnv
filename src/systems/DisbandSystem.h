//
// Created by Codex on 09/07/2026.
//

#ifndef GAME_ENGINE_DISBANDSYSTEM_H
#define GAME_ENGINE_DISBANDSYSTEM_H

#include "core/Ids.h"
#include "content/units/Unit.h"

class Game;

class DisbandSystem {
public:
    static bool canDisband(const Game& game, PlayerId pid, UnitId unitId);
    static bool disband(Game& game, PlayerId pid, UnitId unitId);
    static int refundStars(const Game& game, UnitId unitId);

private:
    static int costForType(UnitType type);
};

#endif // GAME_ENGINE_DISBANDSYSTEM_H
