//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_RIDINGTECH_H
#define GAME_ENGINE_RIDINGTECH_H
#include "Tech.h"


class RidingTech : public Tech {

    public:
    RidingTech(const Tech* previous);
    RidingTech();

    static const RidingTech& getBase();

};


#endif //GAME_ENGINE_RIDINGTECH_H