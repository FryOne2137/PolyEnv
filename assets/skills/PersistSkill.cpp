#include "PersistSkill.h"
#include <iostream>

PersistSkill::PersistSkill() : Skill("Persist") {}

void PersistSkill::apply() {
    std::cout << "Persist skill activated" << std::endl;
}
