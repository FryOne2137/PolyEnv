//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "ForestryTech.h"
#include "HuntingTech.h"

ForestryTech::ForestryTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Archery";
}

ForestryTech::ForestryTech()
    : Tech(2, &HuntingTech::getBase())
{
    name = "Archery";
}

const ForestryTech& ForestryTech::getBase() {
    static ForestryTech instance;
    return instance;
}