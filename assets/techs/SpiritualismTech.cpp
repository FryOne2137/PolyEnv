//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "SpiritualismTech.h"
#include "ArcheryTech.h"

SpiritualismTech::SpiritualismTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Spiritualism";
}

SpiritualismTech::SpiritualismTech()
    : Tech(3, &ArcheryTech::getBase())
{
    name = "Spiritualism";
}

const SpiritualismTech& SpiritualismTech::getBase() {
    static SpiritualismTech instance;
    return instance;
}