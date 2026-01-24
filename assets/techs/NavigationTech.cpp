#include "NavigationTech.h"
#include "SailingTech.h"

NavigationTech::NavigationTech()
    : Tech(3, &SailingTech::getBase())
{
    name = "Navigation";
}

const NavigationTech& NavigationTech::getBase() {
    static NavigationTech instance;
    return instance;
}