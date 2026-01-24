//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_BUILDINGTYPEENUM_H
#define GAME_ENGINE_BUILDINGTYPEENUM_H
#include <cstdint>


enum class BuildingTypeEnum : uint8_t {
    None = 0,

    // Resource Buildings
    Farm        = 1,
    Forge       = 2,
    LumberHut   = 3,
    Market      = 4,
    Mine        = 5,
    Port        = 6,
    Sawmill     = 7,
    Windmill    = 8,
    Lighthouse=16,

    // Monuments
    AltarOfPeace      = 9,
    EmperorsTomb      = 10,
    EyeOfGod          = 11,
    GateOfPower       = 12,
    GrandBazaar       = 13,
    ParkOfFortune     = 14,
    TowerOfWisdom     = 15
};


#endif //GAME_ENGINE_BUILDINGTYPEENUM_H