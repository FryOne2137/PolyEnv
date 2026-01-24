//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_SINGLEBUILDING_H
#define GAME_ENGINE_SINGLEBUILDING_H
#include "Building.h"


class SingleBuilding:public Building {
    public:
    int getPopulationYield() const;


protected:
    int populationYield;
    int price;

};


#endif //GAME_ENGINE_SINGLEBUILDING_H