#include "ExplodeSkill.h"
#include <iostream>

ExplodeSkill::ExplodeSkill() : Skill("Explode") {}

void ExplodeSkill::apply() {
    std::cout << "Explode skill activated" << std::endl;
}
