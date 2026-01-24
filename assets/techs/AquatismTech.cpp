#include "AquatismTech.h"
#include "RammingTech.h"

AquatismTech::AquatismTech()
    : Tech(3, &RammingTech::getBase())
{
    name = "Aquatism";
}

const AquatismTech& AquatismTech::getBase() {
    static AquatismTech instance;
    return instance;
}