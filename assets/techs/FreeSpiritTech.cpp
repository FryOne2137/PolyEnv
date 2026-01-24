#include "FreeSpiritTech.h"
#include "RidingTech.h"

FreeSpiritTech::FreeSpiritTech()
    : Tech(2, &RidingTech::getBase())
{
    name = "Free Spirit";
}

const FreeSpiritTech& FreeSpiritTech::getBase() {
    static FreeSpiritTech instance;
    return instance;
}