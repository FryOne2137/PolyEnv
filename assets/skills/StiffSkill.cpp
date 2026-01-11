#include "StiffSkill.h"
#include <iostream>

StiffSkill::StiffSkill() : Skill("Stiff") {}

void StiffSkill::apply() {
    std::cout << "Stiff skill activated" << std::endl;
}
