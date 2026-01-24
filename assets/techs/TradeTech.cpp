#include "TradeTech.h"
#include "RoadsTech.h"

TradeTech::TradeTech()
    : Tech(3, &RoadsTech::getBase())
{
    name = "Trade";
}

const TradeTech& TradeTech::getBase() {
    static TradeTech instance;
    return instance;
}