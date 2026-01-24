#include "MeditationTech.h"
#include "ClimbingTech.h"

MeditationTech::MeditationTech()
    : Tech(2, &ClimbingTech::getBase())
{
    name = "Meditation";
}

const MeditationTech& MeditationTech::getBase() {
    static MeditationTech instance;
    return instance;
}