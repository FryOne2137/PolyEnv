//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "RammingTech.h"
#include "FishingTech.h"

RammingTech::RammingTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Ramming";
}

RammingTech::RammingTech()
    : Tech(2, &FishingTech::getBase())
{
    name = "Ramming";
}

const RammingTech& RammingTech::getBase() {
    static RammingTech instance;
    return instance;
}