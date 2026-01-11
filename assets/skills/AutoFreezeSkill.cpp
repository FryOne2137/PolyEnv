#include "AutoFreezeSkill.h"
#include <iostream>

AutoFreezeSkill::AutoFreezeSkill() : Skill("AutoFreeze") {}

void AutoFreezeSkill::apply() {
    std::cout << "AutoFreeze skill activated" << std::endl;
}
