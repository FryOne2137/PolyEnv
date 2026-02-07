#ifndef GAME_ENGINE_INTERACTIONSYSTEM_H
#define GAME_ENGINE_INTERACTIONSYSTEM_H

#include <cstdint>

#include "World/Pos.h"
#include "units/Unit.h" // UnitId

class Game;
class Player;

class InteractionSystem {
public:
    static void onUnitEnteredTile(Game& game, UnitId unitId, Pos pos);
    static void handleCityCapture(Game& game, UnitId unitId, Pos pos);
    static void handleVillage(Game& game, UnitId unitId, Pos pos);
    static void handleStarfish(Game& game, UnitId unitId, Pos pos);
    static void handleRuin(Game& game, UnitId unitId, Pos pos);


    enum class RuinReward : uint8_t {
        Stars,
        Tech,
        Population,
        Explorer,
        VeteranUnit
    };

private:
    static RuinReward rollRuinReward(Game& game, PlayerId pid, Pos ruinPos);
};

#endif //GAME_ENGINE_INTERACTIONSYSTEM_H