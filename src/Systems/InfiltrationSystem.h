//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#ifndef GAME_ENGINE_INFILTRATIONSYSTEM_H
#define GAME_ENGINE_INFILTRATIONSYSTEM_H

#include "Game.h"

class InfiltrationSystem {
public:
    static bool infiltrateCity(Game& game, UnitId cloakId, CityId cityId);

};


#endif //GAME_ENGINE_INFILTRATIONSYSTEM_H