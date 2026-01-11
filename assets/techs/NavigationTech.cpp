//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "NavigationTech.h"
#include "SailingTech.h"

NavigationTech::NavigationTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Navigation";
}

NavigationTech::NavigationTech()
    : Tech(3, &SailingTech::getBase())
{
    name = "Navigation";
}

const NavigationTech& NavigationTech::getBase() {
    static NavigationTech instance;
    return instance;
}