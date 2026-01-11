//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_TRADETECH_H
#define GAME_ENGINE_TRADETECH_H
#include "Tech.h"


class TradeTech:public Tech {
    public:
    TradeTech(const Tech* previous);
    TradeTech();
    static const TradeTech& getBase();

};


#endif //GAME_ENGINE_TRADETECH_H