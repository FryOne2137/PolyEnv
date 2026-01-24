#include "PhilosophyTech.h"
#include "MeditationTech.h"

PhilosophyTech::PhilosophyTech()
    : Tech(3, &MeditationTech::getBase())
{
    name = "Philosophy";
}

const PhilosophyTech& PhilosophyTech::getBase() {
    static PhilosophyTech instance;
    return instance;
}