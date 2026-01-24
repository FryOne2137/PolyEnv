#include "RammingTech.h"
#include "FishingTech.h"

RammingTech::RammingTech()
    : Tech(2, &FishingTech::getBase())
{
    name = "Ramming";
}

const RammingTech& RammingTech::getBase() {
    static RammingTech instance;
    return instance;
}