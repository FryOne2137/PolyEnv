#include "SwarmSkill.h"
#include <iostream>

SwarmSkill::SwarmSkill() : Skill("Swarm") {}

void SwarmSkill::apply() {
    std::cout << "Swarm skill activated" << std::endl;
}
