//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_RESOURCESENUM_H
#define GAME_ENGINE_RESOURCESENUM_H

#include <cstdint>

enum class ResourcesEnum : uint8_t {
    None        = 0,

    Fish        = 0,
    Whale       = 1,

    Fruit       = 3,
    Crops       = 4,
    Animal     =  5,

    Metal       = 6,
};

#endif //GAME_ENGINE_RESOURCESENUM_H