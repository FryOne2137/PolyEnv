//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_ROADBRIDGEENUM_H
#define GAME_ENGINE_ROADBRIDGEENUM_H
#include <cstdint>


enum class RoadBridgeEnum : uint8_t{
    None = 0,
    Road = 1,
    Bridge = 2,
    WaterConnection=3,
};


#endif //GAME_ENGINE_ROADBRIDGEENUM_H