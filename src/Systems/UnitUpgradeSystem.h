//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//

#ifndef GAME_ENGINE_UNITUPGRADESYSTEM_H
#define GAME_ENGINE_UNITUPGRADESYSTEM_H

#include <cstdint>

#include "units/Unit.h" // UnitId

class Game;

class UnitUpgradeSystem {
public:
    static bool canUpgradeRaftToScout(const Game& game, UnitId unitId);
    static bool canUpgradeRaftToRammer(const Game& game, UnitId unitId);
    static bool canUpgradeRaftToBomber(const Game& game, UnitId unitId);

    static bool upgradeRaftToScout(Game& game, UnitId unitId);
    static bool upgradeRaftToRammer(Game& game, UnitId unitId);
    static bool upgradeRaftToBomber(Game& game, UnitId unitId);

    static bool canUnitBecomeVeteran(const Game& game, UnitId unitId);
    static bool becomeVeteran(Game& game, UnitId unitId);
private:


};


#endif //GAME_ENGINE_UNITUPGRADESYSTEM_H