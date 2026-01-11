//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_CLIMBINGTECH_H
#define GAME_ENGINE_CLIMBINGTECH_H

#include "Tech.h"


class ClimbingTech:public Tech{

public:
    ClimbingTech();
    ClimbingTech(const Tech* previous);
    static const ClimbingTech& getBase();


};


#endif //GAME_ENGINE_CLIMBINGTECH_H