//
// Created by Fryderyk Niedzwiecki on 01/02/2026.
//

#ifndef GAME_ENGINE_REWARDSYSTEM_H
#define GAME_ENGINE_REWARDSYSTEM_H


#include "World/Pos.h"
#include "units/Unit.h" // UnitId
class Game;


class RewardSystem {
public:
    static bool rewardRuin(Game& game, PlayerId pid, Pos pos);
    static bool rewardStarfish(Game& game, PlayerId pid, Pos pos);
private:

};


#endif //GAME_ENGINE_REWARDSYSTEM_H