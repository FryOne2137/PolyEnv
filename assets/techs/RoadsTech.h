//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_ROADSTECH_H
#define GAME_ENGINE_ROADSTECH_H
#include "Tech.h"


class RoadsTech: public Tech {
public:
    RoadsTech(const Tech* previous);
    RoadsTech();
    static const RoadsTech& getBase();

};


#endif //GAME_ENGINE_ROADSTECH_H