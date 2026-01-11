//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_CONSTRUCTIONTECH_H
#define GAME_ENGINE_CONSTRUCTIONTECH_H
#include "Tech.h"


class ConstructionTech :public Tech{
public:
    ConstructionTech(const Tech* previous);
    ConstructionTech();
    static const ConstructionTech& getBase();

};


#endif //GAME_ENGINE_CONSTRUCTIONTECH_H