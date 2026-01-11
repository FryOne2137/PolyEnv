#include "HideSkill.h"
#include <iostream>

HideSkill::HideSkill() : Skill("Hide") {}

void HideSkill::apply() {
    std::cout << "Hide skill activated" << std::endl;
}
