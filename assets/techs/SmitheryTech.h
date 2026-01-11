//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_SMITHERYTECH_H
#define GAME_ENGINE_SMITHERYTECH_H
#include "Tech.h"


class SmitheryTech:public Tech {
public:
    SmitheryTech(const Tech* previous);
    SmitheryTech();
    static const SmitheryTech& getBase();
};


#endif //GAME_ENGINE_SMITHERYTECH_H