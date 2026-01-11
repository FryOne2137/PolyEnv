//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "AquatismTech.h"
#include "RammingTech.h"

AquatismTech::AquatismTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Aquatism";
}

AquatismTech::AquatismTech()
    : Tech(3, &RammingTech::getBase())
{
    name = "Aquatism";
}

const AquatismTech& AquatismTech::getBase() {
    static AquatismTech instance;
    return instance;
}