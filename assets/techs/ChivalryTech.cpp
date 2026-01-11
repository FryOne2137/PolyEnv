//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "ChivalryTech.h"

#include "FreeSpiritTech.h"

ChivalryTech::ChivalryTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Chivalry";
}

ChivalryTech::ChivalryTech()
    : Tech(3, &FreeSpiritTech::getBase())
{
    name = "Chivalry";
}

const ChivalryTech& ChivalryTech::getBase() {
    static ChivalryTech instance;
    return instance;
}