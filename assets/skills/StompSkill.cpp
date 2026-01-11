#include "StompSkill.h"
#include <iostream>

StompSkill::StompSkill() : Skill("Stomp") {}

void StompSkill::apply() {
    std::cout << "Stomp skill activated" << std::endl;
}
