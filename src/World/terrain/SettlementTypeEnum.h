//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_SETTLEMENTTYPE_H
#define GAME_ENGINE_SETTLEMENTTYPE_H
#include <cstdint>


enum class SettlementTypeEnum : uint8_t {
    None = 0,
    Village = 1,
    City = 2,
    Starfish = 3,
    Ruin = 4,
};


#endif //GAME_ENGINE_SETTLEMENTTYPE_H