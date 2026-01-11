#include "AutoFloodSkill.h"
#include <iostream>

AutoFloodSkill::AutoFloodSkill() : Skill("AutoFlood") {}

void AutoFloodSkill::apply() {
    std::cout << "AutoFlood skill activated" << std::endl;
}
