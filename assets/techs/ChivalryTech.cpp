#include "ChivalryTech.h"
#include "FreeSpiritTech.h"

ChivalryTech::ChivalryTech()
    : Tech(3, &FreeSpiritTech::getBase())
{
    name = "Chivalry";
}

const ChivalryTech& ChivalryTech::getBase() {
    static ChivalryTech instance;
    return instance;
}