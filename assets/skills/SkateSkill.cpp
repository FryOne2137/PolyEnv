#include "SkateSkill.h"
#include <iostream>

SkateSkill::SkateSkill() : Skill("Skate") {}

void SkateSkill::apply() {
    std::cout << "Skate skill activated" << std::endl;
}
