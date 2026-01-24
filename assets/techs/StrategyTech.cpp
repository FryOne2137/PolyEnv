#include "StrategyTech.h"
#include "OrganizationTech.h"

StrategyTech::StrategyTech()
    : Tech(2, &OrganizationTech::getBase())
{
    name = "Strategy";
}

const StrategyTech& StrategyTech::getBase() {
    static StrategyTech instance;
    return instance;
}