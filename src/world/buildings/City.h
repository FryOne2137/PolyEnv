//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_CITY_H
#define GAME_ENGINE_CITY_H

#include <list>

#include "Village.h"
#include <string>
#include <units/Unit.h>

#include "UpgradeRewardEnum.h"

class City : Village{

public:
    int getMaxNumOfUnits();
    int getPopulation();

    int getCityLevel();
    int getStarsPerRound();

    int getPopulationNeededToUpgrade() const;

    bool upgrade();

    bool applyLevelUpReward(UpgradeRewardEnum upgradeBonus);

    std::string getName();
    bool addUnit(Unit* unit);



private:
    int cityId;
    int cityLevel;
    int starsPerRound;
    int population;

    bool hasCityWall;
    bool hasWorkshop = false;

    Player *owner;

    std::string name;
    std::vector<Unit*> units;

    //zabezpieczenie przed 2 dostaniem nagrod za level up
    bool levelUpRewardPending = false;
    int  pendingRewardLevel = 0;

};


#endif //GAME_ENGINE_CITY_H