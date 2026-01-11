//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_MEDITATIONTECH_H
#define GAME_ENGINE_MEDITATIONTECH_H
#include "Tech.h"


class MeditationTech:public Tech {
public:
    MeditationTech(const Tech* previous);
    MeditationTech();
    static const MeditationTech& getBase();

};


#endif //GAME_ENGINE_MEDITATIONTECH_H