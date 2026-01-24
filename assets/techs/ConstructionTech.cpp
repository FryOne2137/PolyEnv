#include "ConstructionTech.h"
#include "FarmingTech.h"

ConstructionTech::ConstructionTech()
    : Tech(3, &FarmingTech::getBase())
{
    name = "Construction";
}

const ConstructionTech& ConstructionTech::getBase() {
    static ConstructionTech instance;
    return instance;
}