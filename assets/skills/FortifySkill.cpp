#include "FortifySkill.h"
#include <iostream>

FortifySkill::FortifySkill() : Skill("Fortify") {}

void FortifySkill::apply() {
    std::cout << "Fortify skill activated" << std::endl;
}
