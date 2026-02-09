//
// Created by Fryderyk Niedzwiecki on 08/02/2026.
//

#ifndef GAME_ENGINE_GAMEDATASYSTEM_H
#define GAME_ENGINE_GAMEDATASYSTEM_H

#include <string>

#include "../content/units/Unit.h"

class GameDataSystem {
public:

    static void loadUnits(const std::string& path);
    static const Unit& getUnitTemplate(UnitType type);
    static void applyUnitTemplate(Unit& u);
    static int getUnitCost(UnitType type);
};

#endif // GAME_ENGINE_GAMEDATASYSTEM_H