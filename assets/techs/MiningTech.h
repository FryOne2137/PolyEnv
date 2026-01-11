//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_MININGTECH_H
#define GAME_ENGINE_MININGTECH_H
#include "Tech.h"


class MiningTech : public Tech{
public:
    MiningTech(const Tech* previous);
    MiningTech();
    static const MiningTech& getBase();

};


#endif //GAME_ENGINE_MININGTECH_H