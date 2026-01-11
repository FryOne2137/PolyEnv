//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "HuntingTech.h"

HuntingTech::HuntingTech()
    : Tech(1, nullptr)
{
    name = "Forestry";
}

const HuntingTech& HuntingTech::getBase() {
    static HuntingTech instance;
    return instance;
}