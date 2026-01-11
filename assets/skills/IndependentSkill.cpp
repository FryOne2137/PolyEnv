#include "IndependentSkill.h"
#include <iostream>

IndependentSkill::IndependentSkill() : Skill("Independent") {}

void IndependentSkill::apply() {
    std::cout << "Independent skill activated" << std::endl;
}
