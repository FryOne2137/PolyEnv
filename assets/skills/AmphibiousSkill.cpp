#include "AmphibiousSkill.h"
#include <iostream>

AmphibiousSkill::AmphibiousSkill() : Skill("Amphibious") {}

void AmphibiousSkill::apply() {
    std::cout << "Amphibious skill activated" << std::endl;
}
