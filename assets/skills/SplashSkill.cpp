#include "SplashSkill.h"
#include <iostream>

SplashSkill::SplashSkill() : Skill("Splash") {}

void SplashSkill::apply() {
    std::cout << "Splash skill activated" << std::endl;
}
