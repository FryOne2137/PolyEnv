//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_DIPLOMACYTECH_H
#define GAME_ENGINE_DIPLOMACYTECH_H
#include "Tech.h"


class DiplomacyTech:public Tech {
public:
    DiplomacyTech(const Tech* previous);
    DiplomacyTech();
    static const DiplomacyTech& getBase();
};


#endif //GAME_ENGINE_DIPLOMACYTECH_H