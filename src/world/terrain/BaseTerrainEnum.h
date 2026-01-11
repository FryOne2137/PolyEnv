//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_BASETERRAINENUM_H
#define GAME_ENGINE_BASETERRAINENUM_H
#include <cstdint>
#include <type_traits>


enum class BaseTerrainEnum : uint8_t {
    Ocean = 0,
    Water = 1,
    Land  = 2,
    Mountain  = 3,
};


#endif //GAME_ENGINE_BASETERRAINENUM_H