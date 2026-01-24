#include "MiningTech.h"
#include "ClimbingTech.h"

MiningTech::MiningTech()
    : Tech(2, &ClimbingTech::getBase())
{
    name = "Mining";
}

const MiningTech& MiningTech::getBase() {
    static MiningTech instance;
    return instance;
}