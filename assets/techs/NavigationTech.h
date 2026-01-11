//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_NAVIGATIONTECH_H
#define GAME_ENGINE_NAVIGATIONTECH_H
#include "Tech.h"


class NavigationTech : public Tech{
public:
    NavigationTech(const Tech* previous);
    NavigationTech();
    static const NavigationTech& getBase();
};


#endif //GAME_ENGINE_NAVIGATIONTECH_H