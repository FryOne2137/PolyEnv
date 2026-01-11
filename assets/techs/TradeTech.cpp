//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "TradeTech.h"

#include "RoadsTech.h"

TradeTech::TradeTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Trade";
}

TradeTech::TradeTech()
    : Tech(3, &RoadsTech::getBase())
{
    name = "Trade";
}

const TradeTech& TradeTech::getBase() {
    static TradeTech instance;
    return instance;
}