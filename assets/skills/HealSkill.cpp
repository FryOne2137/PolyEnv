#include "HealSkill.h"
#include <iostream>

HealSkill::HealSkill() : Skill("Heal") {}

void HealSkill::apply() {
    std::cout << "Heal skill activated" << std::endl;
}
