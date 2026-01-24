//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_MONUMENT_H
#define GAME_ENGINE_MONUMENT_H
#include "Building.h"


class Monument :public Building{
    public:
    int getId() const;

    protected:
    int id;
    int populationYield=3;
    int price=0;


};


#endif //GAME_ENGINE_MONUMENT_H