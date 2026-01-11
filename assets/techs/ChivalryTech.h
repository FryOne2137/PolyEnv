//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_CHIVALRYTECH_H
#define GAME_ENGINE_CHIVALRYTECH_H
#include "Tech.h"


class ChivalryTech:public Tech {
public:
    ChivalryTech(const Tech* previous);
    ChivalryTech();
    static const ChivalryTech& getBase();

};


#endif //GAME_ENGINE_CHIVALRYTECH_H