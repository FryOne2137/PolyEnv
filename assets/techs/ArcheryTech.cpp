#include "ArcheryTech.h"
#include "HuntingTech.h"

ArcheryTech::ArcheryTech()
    : Tech(2, &HuntingTech::getBase())
{
    name = "Archery";
}

const ArcheryTech& ArcheryTech::getBase() {
    static ArcheryTech instance;
    return instance;
}