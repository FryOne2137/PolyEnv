//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_VISIBILITYENUM_H
#define GAME_ENGINE_VISIBILITYENUM_H

#include <cstdint>

using PlayerIndex = uint8_t;

enum class VisibilityEnum : uint16_t {
    None = 0
};

inline VisibilityEnum operator|(VisibilityEnum a, VisibilityEnum b) {
    return static_cast<VisibilityEnum>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b)
    );
}

inline VisibilityEnum operator&(VisibilityEnum a, VisibilityEnum b) {
    return static_cast<VisibilityEnum>(
        static_cast<uint16_t>(a) & static_cast<uint16_t>(b)
    );
}

inline VisibilityEnum& operator|=(VisibilityEnum& a, VisibilityEnum b) {
    a = a | b;
    return a;
}

inline void reveal(VisibilityEnum& v, PlayerIndex player) {
    v |= static_cast<VisibilityEnum>(uint16_t(1) << player);
}

inline bool isRevealed(VisibilityEnum v, PlayerIndex player) {
    return (static_cast<uint16_t>(v) & (uint16_t(1) << player)) != 0;
}



#endif //GAME_ENGINE_VISIBILITYENUM_H