//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_FREESPIRITTECH_H
#define GAME_ENGINE_FREESPIRITTECH_H
#include "Tech.h"


class FreeSpiritTech:public Tech {
public:
    FreeSpiritTech(const Tech* previous);
    FreeSpiritTech();
    static const FreeSpiritTech& getBase();

};


#endif //GAME_ENGINE_FREESPIRITTECH_H