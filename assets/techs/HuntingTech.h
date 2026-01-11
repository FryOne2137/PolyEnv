//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_HUNTINGTECH_H
#define GAME_ENGINE_HUNTINGTECH_H
#include "Tech.h"


class HuntingTech : public Tech{
    public:
    HuntingTech();
    static const HuntingTech& getBase();


};


#endif //GAME_ENGINE_HUNTINGTECH_H