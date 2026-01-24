#include "SailingTech.h"
#include "FishingTech.h"

SailingTech::SailingTech()
    : Tech(2, &FishingTech::getBase())
{
    name = "Sailing";
}

const SailingTech& SailingTech::getBase() {
    static SailingTech instance;
    return instance;
}