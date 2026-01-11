#include "TentaclesSkill.h"
#include <iostream>

TentaclesSkill::TentaclesSkill() : Skill("Tentacles") {}

void TentaclesSkill::apply() {
    std::cout << "Tentacles skill activated" << std::endl;
}
