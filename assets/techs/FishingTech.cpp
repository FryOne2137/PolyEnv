#include "FishingTech.h"

FishingTech::FishingTech()
    : Tech(1, nullptr)
{
    name = "Fishing";
}

const FishingTech& FishingTech::getBase() {
    static FishingTech instance;
    return instance;
}