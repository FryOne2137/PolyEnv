//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_MATHEMATICSTECH_H
#define GAME_ENGINE_MATHEMATICSTECH_H
#include "Tech.h"


class MathematicsTech:public Tech {
public:
    MathematicsTech(const Tech* previous);
    MathematicsTech();
    static const MathematicsTech& getBase();

};


#endif //GAME_ENGINE_MATHEMATICSTECH_H