//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_UNITSKILLTOOLS_H
#define GAME_ENGINE_UNITSKILLTOOLS_H

#include "UnitSkill.h"
#include <cstdint>

inline uint64_t toMask(UnitSkill s) {
    return static_cast<uint64_t>(s);
}

inline bool hasSkill(uint64_t mask, UnitSkill skill) {
    return (mask & toMask(skill)) != 0;
}

inline void addSkill(uint64_t& mask, UnitSkill skill) {
    mask |= toMask(skill);
}

inline void removeSkill(uint64_t& mask, UnitSkill skill) {
    mask &= ~toMask(skill);
}

inline void clearSkills(uint64_t& mask) {
    mask = 0;
}

#endif //GAME_ENGINE_UNITSKILLTOOLS_H