//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_FISHINGTECH_H
#define GAME_ENGINE_FISHINGTECH_H
#include "Tech.h"


class FishingTech : public Tech {
    public:
    FishingTech(const Tech* previous);
    FishingTech();
    static const FishingTech& getBase();

};


#endif //GAME_ENGINE_FISHINGTECH_H