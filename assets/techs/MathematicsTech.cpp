#include "MathematicsTech.h"
#include "ForestryTech.h"

MathematicsTech::MathematicsTech()
    : Tech(3, &ForestryTech::getBase())
{
    name = "Mathematics";
}

const MathematicsTech& MathematicsTech::getBase() {
    static MathematicsTech instance;
    return instance;
}