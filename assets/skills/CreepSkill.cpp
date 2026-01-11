#include "CreepSkill.h"
#include <iostream>

CreepSkill::CreepSkill() : Skill("Creep") {}

void CreepSkill::apply() {
    std::cout << "Creep skill activated" << std::endl;
}
