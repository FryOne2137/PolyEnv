#include "EatSkill.h"
#include <iostream>

EatSkill::EatSkill() : Skill("Eat") {}

void EatSkill::apply() {
    std::cout << "Eat skill activated" << std::endl;
}
