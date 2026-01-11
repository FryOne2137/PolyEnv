#include "ConvertSkill.h"
#include <iostream>

ConvertSkill::ConvertSkill() : Skill("Convert") {}

void ConvertSkill::apply() {
    std::cout << "Convert skill activated" << std::endl;
}
