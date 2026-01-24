#include "ForestryTech.h"
#include "HuntingTech.h"

ForestryTech::ForestryTech()
    : Tech(2, &HuntingTech::getBase())
{
    name = "Forestry";
}

const ForestryTech& ForestryTech::getBase() {
    static ForestryTech instance;
    return instance;
}