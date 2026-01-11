//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_FORESTRYTECH_H
#define GAME_ENGINE_FORESTRYTECH_H
#include "Tech.h"


class ForestryTech:public Tech{
public:
    ForestryTech(const Tech* previous);
    ForestryTech();
    static const ForestryTech& getBase();

};


#endif //GAME_ENGINE_FORESTRYTECH_H