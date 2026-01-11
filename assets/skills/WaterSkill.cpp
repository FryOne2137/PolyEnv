#include "WaterSkill.h"
#include <iostream>

WaterSkill::WaterSkill() : Skill("Water") {}

void WaterSkill::apply() {
    std::cout << "Water skill activated" << std::endl;
}
