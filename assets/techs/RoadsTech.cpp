//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "RoadsTech.h"


#include "RidingTech.h"

RoadsTech::RoadsTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Archery";
}

RoadsTech::RoadsTech()
    : Tech(2, &RidingTech::getBase())
{
    name = "Archery";
}

const RoadsTech& RoadsTech::getBase() {
    static RoadsTech instance;
    return instance;
}