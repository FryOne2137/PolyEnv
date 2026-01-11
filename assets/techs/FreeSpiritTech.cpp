//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "FreeSpiritTech.h"
#include "RidingTech.h"

FreeSpiritTech::FreeSpiritTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Free Spirit";
}

FreeSpiritTech::FreeSpiritTech()
    : Tech(2, &RidingTech::getBase())
{
    name = "Free Spirit";
}

const FreeSpiritTech& FreeSpiritTech::getBase() {
    static FreeSpiritTech instance;
    return instance;
}