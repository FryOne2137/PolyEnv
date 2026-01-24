#include "DiplomacyTech.h"
#include "StrategyTech.h"

DiplomacyTech::DiplomacyTech()
    : Tech(3, &StrategyTech::getBase())
{
    name = "Diplomacy";
}

const DiplomacyTech& DiplomacyTech::getBase() {
    static DiplomacyTech instance;
    return instance;
}