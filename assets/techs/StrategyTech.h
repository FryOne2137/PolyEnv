//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_STRATEGYTECH_H
#define GAME_ENGINE_STRATEGYTECH_H
#include "Tech.h"


class StrategyTech:public Tech {
public:
    StrategyTech(const Tech* previous);
    StrategyTech();
    static const StrategyTech& getBase();

};


#endif //GAME_ENGINE_STRATEGYTECH_H