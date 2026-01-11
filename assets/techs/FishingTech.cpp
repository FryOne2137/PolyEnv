//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "FishingTech.h"

FishingTech::FishingTech(const Tech* previous)
    : Tech(1, previous)
{
    name = "Fishing";
}

FishingTech::FishingTech()
    : Tech(1, nullptr)
{
    name = "Fishing";
}

const FishingTech& FishingTech::getBase() {
    static FishingTech instance;
    return instance;
}