//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_ADJACENTBUILDING_H
#define GAME_ENGINE_ADJACENTBUILDING_H
#include "Building.h"


class AdjacentBuilding:public Building {
    public:
        int getPopulationYield() const;

    protected:
        int populationYield;

};


#endif //GAME_ENGINE_ADJACENTBUILDING_H