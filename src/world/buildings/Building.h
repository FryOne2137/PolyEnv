//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_BUILDING_H
#define GAME_ENGINE_BUILDING_H
#include "Pos.h"


class Building {
    public:
    Pos getPos();

private:
    int x,y;
};


#endif //GAME_ENGINE_BUILDING_H