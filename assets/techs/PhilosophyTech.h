//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_PHILOSOPHYTECH_H
#define GAME_ENGINE_PHILOSOPHYTECH_H
#include "Tech.h"


class PhilosophyTech :public Tech{
public:
    PhilosophyTech(const Tech* previous);
    PhilosophyTech();
    static const PhilosophyTech& getBase();
};


#endif //GAME_ENGINE_PHILOSOPHYTECH_H