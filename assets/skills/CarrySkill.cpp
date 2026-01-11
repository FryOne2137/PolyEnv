#include "CarrySkill.h"
#include <iostream>

CarrySkill::CarrySkill() : Skill("Carry") {}

void CarrySkill::apply() {
    std::cout << "Carry skill activated" << std::endl;
}
