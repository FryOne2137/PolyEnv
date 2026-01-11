//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#include "Unit.h"
#include "../assets/skills/UnitSkillTools.h"

bool Unit::hasSkill(UnitSkill skill) const {
    return ::hasSkill(skillMask, skill);
}

