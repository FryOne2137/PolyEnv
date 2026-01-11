#include "EscapeSkill.h"
#include <iostream>

EscapeSkill::EscapeSkill() : Skill("Escape") {}

void EscapeSkill::apply() {
    std::cout << "Escape skill activated" << std::endl;
}
