//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#include "AlgaeSkill.h"
#include <iostream>

AlgaeSkill::AlgaeSkill() : Skill("Algae") {}

void AlgaeSkill::apply() {
    std::cout << "Algae skill activated" << std::endl;
}