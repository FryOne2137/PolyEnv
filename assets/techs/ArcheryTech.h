//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_ARCHERYTECH_H
#define GAME_ENGINE_ARCHERYTECH_H
#include "Tech.h"


class ArcheryTech:public Tech{
    public:
    ArcheryTech(const Tech* previous);
    ArcheryTech();
    static const ArcheryTech& getBase();

};


#endif //GAME_ENGINE_ARCHERYTECH_H