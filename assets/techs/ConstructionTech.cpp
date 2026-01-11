//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "ConstructionTech.h"
#include "FarmingTech.h"

ConstructionTech::ConstructionTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Construction";
}

ConstructionTech::ConstructionTech()
    : Tech(3, &FarmingTech::getBase())
{
    name = "Construction";
}

const ConstructionTech& ConstructionTech::getBase() {
    static ConstructionTech instance;
    return instance;
}