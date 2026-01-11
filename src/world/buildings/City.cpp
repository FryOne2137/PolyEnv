//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "City.h"

int City::getMaxNumOfUnits() {
    return City::cityLevel+1;
}

bool City::upgrade() {
    const int needed = getPopulationNeededToUpgrade();
    if (population < needed) return false;

    population -= needed;
    cityLevel += 1;
    starsPerRound += 1;

    levelUpRewardPending = true;
    pendingRewardLevel = cityLevel;
    return true;
}
int City::getPopulationNeededToUpgrade() const {
    return City::cityLevel+1;

}


std::string City::getName() {
    return City::name;
}

bool City::applyLevelUpReward(UpgradeRewardEnum upgradeBonus) {

    if (!levelUpRewardPending) return false;
    if (pendingRewardLevel != cityLevel) return false;

    const int newLevel = cityLevel;

    auto isAllowedForLevel = [&](UpgradeRewardEnum b) -> bool {
        if (newLevel == 2) return b == UpgradeRewardEnum::Workshop || b == UpgradeRewardEnum::Explorer;
        if (newLevel == 3) return b == UpgradeRewardEnum::CityWall  || b == UpgradeRewardEnum::FiveStars;
        if (newLevel == 4) return b == UpgradeRewardEnum::PopulationGrowth || b == UpgradeRewardEnum::BorderGrowth;
        if (newLevel >= 5) return b == UpgradeRewardEnum::Park || b == UpgradeRewardEnum::SuperUnit;
        return false;
    };

    if (!isAllowedForLevel(upgradeBonus)) return false;

    bool applied = false;

    switch (upgradeBonus) {
        case UpgradeRewardEnum::Workshop:
            if (!hasWorkshop) {
                hasWorkshop = true;
                starsPerRound += 1;
            }
            applied = true;
            break;

        case UpgradeRewardEnum::Explorer:
            // TODO
            applied = true;
            break;

        case UpgradeRewardEnum::CityWall:
            if (!hasCityWall) {
                hasCityWall = true;
            }
            applied = true;
            break;

        case UpgradeRewardEnum::FiveStars:
            if (owner) {
                owner->addStars(5);
            }
            applied = true;
            break;

        case UpgradeRewardEnum::PopulationGrowth:
            population += 3;
            applied = true;
            break;

        case UpgradeRewardEnum::BorderGrowth:
            // TODO
            applied = true;
            break;

        case UpgradeRewardEnum::Park:
            starsPerRound += 1;
            applied = true;
            break;

        case UpgradeRewardEnum::SuperUnit:
            // TODO
            applied = true;
            break;

        default:
            applied = false;
            break;
    }

    if (!applied) return false;

    levelUpRewardPending = false;
    pendingRewardLevel = 0;

    return true;
}


