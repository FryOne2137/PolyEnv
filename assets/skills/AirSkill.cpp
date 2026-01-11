#include "AirSkill.h"
#include <iostream>

AirSkill::AirSkill() : Skill("Air") {}

void AirSkill::apply() {
    std::cout << "Air skill activated" << std::endl;
}
