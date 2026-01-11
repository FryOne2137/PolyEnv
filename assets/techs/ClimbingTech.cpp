//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "ClimbingTech.h"

ClimbingTech::ClimbingTech()
    : Tech(1, nullptr)
{
    name = "Climbing";
}

ClimbingTech::ClimbingTech(const Tech* previous)
    : Tech(1, previous)
{
    name = "Climbing";
}

const ClimbingTech& ClimbingTech::getBase() {
    static ClimbingTech instance;
    return instance;
}