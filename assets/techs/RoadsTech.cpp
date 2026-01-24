#include "RoadsTech.h"
#include "RidingTech.h"

RoadsTech::RoadsTech()
    : Tech(2, &RidingTech::getBase())
{
    name = "Roads";
}

const RoadsTech& RoadsTech::getBase() {
    static RoadsTech instance;
    return instance;
}