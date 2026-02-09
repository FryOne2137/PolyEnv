//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//

#ifndef GAME_ENGINE_UNITSPAWNSYSTEM_H
#define GAME_ENGINE_UNITSPAWNSYSTEM_H

#include "../game/Game.h"
#include "systems/VisionSystem.h"
#include "../content/units/UnitFactory.h"

#include "world/Tile.h"
#include "terrain/SettlementTypeEnum.h"


class UnitSpawnSystem {
    public:
    static UnitId spawnUnit(Game& game, Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately);
    static UnitId spawnUnitForced(Game& game, Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately, bool makeVeteran);

};


#endif //GAME_ENGINE_UNITSPAWNSYSTEM_H