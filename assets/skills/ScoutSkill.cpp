#include "ScoutSkill.h"
#include <iostream>

ScoutSkill::ScoutSkill() : Skill("Scout") {}

void ScoutSkill::apply() {
    std::cout << "Scout skill activated" << std::endl;
}
