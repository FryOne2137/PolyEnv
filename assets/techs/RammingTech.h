//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_RAMMINGTECH_H
#define GAME_ENGINE_RAMMINGTECH_H
#include "Tech.h"


class RammingTech : public Tech{
public:
    RammingTech(const Tech* previous);
    RammingTech();
    static const RammingTech& getBase();

};


#endif //GAME_ENGINE_RAMMINGTECH_H