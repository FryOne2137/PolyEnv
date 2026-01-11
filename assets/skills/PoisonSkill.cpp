#include "PoisonSkill.h"
#include <iostream>

PoisonSkill::PoisonSkill() : Skill("Poison") {}

void PoisonSkill::apply() {
    std::cout << "Poison skill activated" << std::endl;
}
