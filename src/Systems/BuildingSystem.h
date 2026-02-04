//
// Created by Fryderyk Niedzwiecki on 25/01/2026.
//

#ifndef GAME_ENGINE_BUILDINGSYSTEM_H
#define GAME_ENGINE_BUILDINGSYSTEM_H
#include "Game.h"
#include "terrain/ResourcesEnum.h"




class BuildingSystem {
public:
    static bool canBuild(const Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type);
    static bool build(Game& game, PlayerId pid, Pos pos, BuildingTypeEnum type);

    bool buildMonument(Game &game, PlayerId pid, Pos pos, BuildingTypeEnum type);
};

#endif //GAME_ENGINE_BUILDINGSYSTEM_H