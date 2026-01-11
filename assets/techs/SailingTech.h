//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_SAILINGTECH_H
#define GAME_ENGINE_SAILINGTECH_H
#include "Tech.h"


class SailingTech :public Tech{
public:
    SailingTech(const Tech* previous);
    SailingTech();
    static const SailingTech& getBase();

};


#endif //GAME_ENGINE_SAILINGTECH_H