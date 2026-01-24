//
// Created by Fryderyk Niedzwiecki on 16/01/2026.
//

#ifndef GAME_ENGINE_IDS_H
#define GAME_ENGINE_IDS_H


#pragma once
#include <cstdint>

using PlayerId = uint8_t;
static constexpr PlayerId kNoPlayer = 0xFF;

using UnitId = uint16_t;
static constexpr UnitId kNoUnit = 0xFFFF;

using CityId = uint16_t;
static constexpr CityId kNoCity = 0xFFFF;

#endif //GAME_ENGINE_IDS_H