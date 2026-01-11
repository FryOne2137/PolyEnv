#include "DoubleAttackSkill.h"
#include <iostream>

DoubleAttackSkill::DoubleAttackSkill() : Skill("DoubleAttack") {}

void DoubleAttackSkill::apply() {
    std::cout << "DoubleAttack skill activated" << std::endl;
}
