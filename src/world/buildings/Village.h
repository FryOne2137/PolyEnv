//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_VILLAGE_H
#define GAME_ENGINE_VILLAGE_H
#include "Building.h"
#include "Player.h"


class Village : Building {
public:
    bool takeCity(Player owner);

private:
};


#endif //GAME_ENGINE_VILLAGE_H