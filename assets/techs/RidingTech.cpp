#include "RidingTech.h"

RidingTech::RidingTech()
    : Tech(1, nullptr)   // ROOT TECH
{
    name = "Riding";
}

const RidingTech& RidingTech::getBase() {
    static RidingTech instance;
    return instance;
}