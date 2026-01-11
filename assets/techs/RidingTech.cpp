//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "RidingTech.h"

RidingTech::RidingTech(const Tech* previous)
    : Tech(1, previous)
{
    name = "Riding";
}


RidingTech::RidingTech()
    : Tech(1, nullptr)
{
    name = "Riding";
}

const RidingTech& RidingTech::getBase() {
    static RidingTech instance;
    return instance;
}