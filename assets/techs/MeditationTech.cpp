//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "MeditationTech.h"
#include "ClimbingTech.h"

MeditationTech::MeditationTech(const Tech* previous)
    : Tech(2, previous)
{
    name = "Meditation";
}

MeditationTech::MeditationTech()
    : Tech(2, &ClimbingTech::getBase())
{
    name = "Meditation";
}

const MeditationTech& MeditationTech::getBase() {
    static MeditationTech instance;
    return instance;
}
