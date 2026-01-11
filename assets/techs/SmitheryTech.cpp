//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "SmitheryTech.h"
#include "MiningTech.h"

SmitheryTech::SmitheryTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Smithery";
}

SmitheryTech::SmitheryTech()
    : Tech(3, &MiningTech::getBase())
{
    name = "Smithery";
}

const SmitheryTech& SmitheryTech::getBase() {
    static SmitheryTech instance;
    return instance;
}