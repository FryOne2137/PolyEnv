//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "DiplomacyTech.h"
#include "StrategyTech.h"

DiplomacyTech::DiplomacyTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Chivalry";
}

DiplomacyTech::DiplomacyTech()
    : Tech(3, &StrategyTech::getBase())
{
    name = "Chivalry";
}

const DiplomacyTech& DiplomacyTech::getBase() {
    static DiplomacyTech instance;
    return instance;
}