#include "DrenchSkill.h"
#include <iostream>

DrenchSkill::DrenchSkill() : Skill("Drench") {}

void DrenchSkill::apply() {
    std::cout << "Drench skill activated" << std::endl;
}
