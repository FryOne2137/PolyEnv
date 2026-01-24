//
// Created by Fryderyk Niedzwiecki on 13/01/2026.
//

#ifndef GAME_ENGINE_BUILDING_H
#define GAME_ENGINE_BUILDING_H
#include "City.h"
#include "Settlements/Settlement.h"


class Building : public Settlement{
    public:
        City* getOwnerCity() const;
        virtual int getPopulationYield() const;
        int getPrice() const;


protected:
    City* ownerCity = nullptr;
    int price;


};


#endif //GAME_ENGINE_BUILDING_H