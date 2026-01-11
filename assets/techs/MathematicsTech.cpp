//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "MathematicsTech.h"

#include "ForestryTech.h"

MathematicsTech::MathematicsTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Mathematics";
}

MathematicsTech::MathematicsTech()
    : Tech(3, &ForestryTech::getBase())
{
    name = "Mathematics";
}

const MathematicsTech& MathematicsTech::getBase() {
    static MathematicsTech instance;
    return instance;
}