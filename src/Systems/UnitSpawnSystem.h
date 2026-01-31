//
// Created by Fryderyk Niedzwiecki on 28/01/2026.
//

#ifndef GAME_ENGINE_UNITSPAWNSYSTEM_H
#define GAME_ENGINE_UNITSPAWNSYSTEM_H

#include "Game.h"
#include "Systems/VisionSystem.h"
#include "units/UnitFactory.h"

#include "World/Tile.h"
#include "terrain/SettlementTypeEnum.h"


class UnitSpawnSystem {
    public:
    static UnitId spawnUnit(Game& game, Map& map, UnitType type, PlayerId owner, Pos pos, bool canActImmediately);

};


#endif //GAME_ENGINE_UNITSPAWNSYSTEM_H