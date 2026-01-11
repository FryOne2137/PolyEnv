#include "GrowSkill.h"
#include <iostream>

GrowSkill::GrowSkill() : Skill("Grow") {}

void GrowSkill::apply() {
    std::cout << "Grow skill activated" << std::endl;
}
