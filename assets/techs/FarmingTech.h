//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_FARMINGTECH_H
#define GAME_ENGINE_FARMINGTECH_H
#include "Tech.h"


class FarmingTech : public Tech{
public:
    FarmingTech(const Tech* previous);
    FarmingTech();
    static const FarmingTech& getBase();

};


#endif //GAME_ENGINE_FARMINGTECH_H