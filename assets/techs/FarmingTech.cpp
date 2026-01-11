//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "FarmingTech.h"
#include "OrganizationTech.h"

FarmingTech::FarmingTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Farming";
}

FarmingTech::FarmingTech()
    : Tech(2, &OrganizationTech::getBase())
{
    name = "Farming";
}

const FarmingTech& FarmingTech::getBase() {
    static FarmingTech instance;
    return instance;
}