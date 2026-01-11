#include "FreezeSkill.h"
#include <iostream>

FreezeSkill::FreezeSkill() : Skill("Freeze") {}

void FreezeSkill::apply() {
    std::cout << "Freeze skill activated" << std::endl;
}
