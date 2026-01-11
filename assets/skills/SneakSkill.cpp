#include "SneakSkill.h"
#include <iostream>

SneakSkill::SneakSkill() : Skill("Sneak") {}

void SneakSkill::apply() {
    std::cout << "Sneak skill activated" << std::endl;
}
