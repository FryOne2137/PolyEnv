//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "StrategyTech.h"

#include "OrganizationTech.h"

StrategyTech::StrategyTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Strategy";
}

StrategyTech::StrategyTech()
    : Tech(2, &OrganizationTech::getBase())
{
    name = "Strategy";
}

const StrategyTech& StrategyTech::getBase() {
    static StrategyTech instance;
    return instance;
}