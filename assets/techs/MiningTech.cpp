//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "MiningTech.h"
#include "ClimbingTech.h"

MiningTech::MiningTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Mining";
}

MiningTech::MiningTech()
    : Tech(2, &ClimbingTech::getBase())
{
    name = "Mining";
}

const MiningTech& MiningTech::getBase() {
    static MiningTech instance;
    return instance;
}