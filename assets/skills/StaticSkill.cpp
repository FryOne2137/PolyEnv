#include "StaticSkill.h"
#include <iostream>

StaticSkill::StaticSkill() : Skill("Static") {}

void StaticSkill::apply() {
    std::cout << "Static skill activated" << std::endl;
}
