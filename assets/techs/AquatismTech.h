//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_AQUATISMTECH_H
#define GAME_ENGINE_AQUATISMTECH_H
#include "Tech.h"


class AquatismTech :public Tech{
public:
    AquatismTech(const Tech* previous);
    AquatismTech();
    static const AquatismTech& getBase();
};


#endif //GAME_ENGINE_AQUATISMTECH_H