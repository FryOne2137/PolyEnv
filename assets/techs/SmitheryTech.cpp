#include "SmitheryTech.h"
#include "MiningTech.h"

SmitheryTech::SmitheryTech()
    : Tech(3, &MiningTech::getBase())
{
    name = "Smithery";
}

const SmitheryTech& SmitheryTech::getBase() {
    static SmitheryTech instance;
    return instance;
}