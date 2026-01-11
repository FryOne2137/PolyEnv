//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "ArcheryTech.h"

#include "HuntingTech.h"

ArcheryTech::ArcheryTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Archery";
}

ArcheryTech::ArcheryTech()
    : Tech(2, &HuntingTech::getBase())
{
    name = "Archery";
}

const ArcheryTech& ArcheryTech::getBase() {
    static ArcheryTech instance;
    return instance;
}