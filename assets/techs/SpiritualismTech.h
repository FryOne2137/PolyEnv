//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_SPIRITUALISMTECH_H
#define GAME_ENGINE_SPIRITUALISMTECH_H
#include "Tech.h"


class SpiritualismTech :public Tech{
public:
    SpiritualismTech(const Tech* previous);
    SpiritualismTech();
    static const SpiritualismTech& getBase();

};


#endif //GAME_ENGINE_SPIRITUALISMTECH_H