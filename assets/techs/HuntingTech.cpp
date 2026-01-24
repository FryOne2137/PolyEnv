#include "HuntingTech.h"

HuntingTech::HuntingTech()
    : Tech(1, nullptr)
{
    name = "Hunting";
}

const HuntingTech& HuntingTech::getBase() {
    static HuntingTech instance;
    return instance;
}