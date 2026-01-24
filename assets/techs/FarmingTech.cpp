#include "FarmingTech.h"
#include "OrganizationTech.h"

FarmingTech::FarmingTech()
    : Tech(2, &OrganizationTech::getBase())
{
    name = "Farming";
}

const FarmingTech& FarmingTech::getBase() {
    static FarmingTech instance;
    return instance;
}