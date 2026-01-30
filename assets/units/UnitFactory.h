//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_UNITFACTORY_H
#define GAME_ENGINE_UNITFACTORY_H

#include <cstdint>

#include "Unit.h"

class UnitFactory {
public:
    static Unit create(UnitType type, PlayerId ownerId, Pos pos);
    static int getUnitCost(UnitType type);

private:
    static void applyBaseStats(Unit& u);
};

#endif //GAME_ENGINE_UNITFACTORY_H