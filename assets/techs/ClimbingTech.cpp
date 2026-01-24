#include "ClimbingTech.h"

ClimbingTech::ClimbingTech()
    : Tech(1, nullptr)
{
    name = "Climbing";
}

const ClimbingTech& ClimbingTech::getBase() {
    static ClimbingTech instance;
    return instance;
}