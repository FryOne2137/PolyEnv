#include "InfiltrateSkill.h"
#include <iostream>

InfiltrateSkill::InfiltrateSkill() : Skill("Infiltrate") {}

void InfiltrateSkill::apply() {
    std::cout << "Infiltrate skill activated" << std::endl;
}
