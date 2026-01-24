#include "SpiritualismTech.h"
#include "ArcheryTech.h"

SpiritualismTech::SpiritualismTech()
    : Tech(3, &ArcheryTech::getBase())
{
    name = "Spiritualism";
}

const SpiritualismTech& SpiritualismTech::getBase() {
    static SpiritualismTech instance;
    return instance;
}