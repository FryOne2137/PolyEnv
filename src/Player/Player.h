//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_PLAYER_H
#define GAME_ENGINE_PLAYER_H
#include <vector>

#include "Unit.h"
#include "techs/Tech.h"
#include "../world/buildings/City.h"


class Player {
public:
    bool buyTech(const Tech* tech);
    bool endTurn();
    int getPlayerId() const;
    int getStarBalance() const;
    std::vector<Tech*> getTechs() const;
    std::vector<City*> getCities() const;
    std::vector<Unit*> getUnits() const;

    bool collectStarsFromCities();
    bool addStars(int starAmount);


private:
    int playerId;
    int starBalance;
    City* capital;
    std::vector<Tech*> techs;
    std::vector<Unit*> units;
    std::vector<City*> cities;

    bool hasTech(const Tech* tech) const;


};


#endif //GAME_ENGINE_PLAYER_H