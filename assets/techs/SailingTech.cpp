//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "SailingTech.h"

#include "FishingTech.h"

SailingTech::SailingTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Sailing";
}

SailingTech::SailingTech()
    : Tech(2, &FishingTech::getBase())
{
    name = "Sailing";
}

const SailingTech& SailingTech::getBase() {
    static SailingTech instance;
    return instance;
}