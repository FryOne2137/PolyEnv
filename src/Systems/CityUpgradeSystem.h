//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#ifndef GAME_ENGINE_CITYUPGRADESYSTEM_H

#define GAME_ENGINE_CITYUPGRADESYSTEM_H
#include <cstdint>
#include "Core/Ids.h"

class Game;


enum class CityUpgradeChoice : uint8_t {
    None = 0,

    // Level 2
    Workshop,
    Explorer,

    // Level 3
    CityWall,
    Resources,

    // Level 4
    PopulationGrowth,
    BorderGrowth,

    // Level 5+
    Park,
    SuperUnit
};

struct CityUpgradeOptions {
    CityUpgradeChoice a = CityUpgradeChoice::None;
    CityUpgradeChoice b = CityUpgradeChoice::None;
};

class CityUpgradeSystem {
public:

    // Returns true if city has enough population to upgrade.
    static bool canUpgrade(const Game& game, CityId cityId);
    static uint8_t getNextLevel(const Game& game, CityId cityId);
    static CityUpgradeOptions getUpgradeOptions(const Game& game, CityId cityId);
    static bool tryUpgrade(Game& game, CityId cityId, CityUpgradeChoice choice);

private:

    // Internal helper: required population for given level.
    static uint8_t requiredPopulationForLevel(uint8_t level);
};


#endif //GAME_ENGINE_CITYUPGRADESYSTEM_H