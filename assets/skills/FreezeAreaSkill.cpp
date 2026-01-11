#include "FreezeAreaSkill.h"
#include <iostream>

FreezeAreaSkill::FreezeAreaSkill() : Skill("FreezeArea") {}

void FreezeAreaSkill::apply() {
    std::cout << "FreezeArea skill activated" << std::endl;
}
