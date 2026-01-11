#include "DashSkill.h"
#include <iostream>

DashSkill::DashSkill() : Skill("Dash") {}

void DashSkill::apply() {
    std::cout << "Dash skill activated" << std::endl;
}
