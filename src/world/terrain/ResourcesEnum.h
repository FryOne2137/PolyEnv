//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_RESOURCESENUM_H
#define GAME_ENGINE_RESOURCESENUM_H

#include <cstdint>

enum class ResourcesEnum : uint16_t {
    None        = 0,

    Fish        = 1u << 0,
    Whale       = 1u << 1,
    Starfish    = 1u << 2,

    Fruit       = 1u << 3,
    Crops       = 1u << 4,
    Animals     = 1u << 5,

    Metal       = 1u << 6,
    Forest      = 1u << 7,
};

#endif //GAME_ENGINE_RESOURCESENUM_H