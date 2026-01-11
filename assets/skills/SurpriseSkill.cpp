#include "SurpriseSkill.h"
#include <iostream>

SurpriseSkill::SurpriseSkill() : Skill("Surprise") {}

void SurpriseSkill::apply() {
    std::cout << "Surprise skill activated" << std::endl;
}
