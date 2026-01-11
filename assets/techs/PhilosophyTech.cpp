//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "PhilosophyTech.h"
#include "MeditationTech.h"

PhilosophyTech::PhilosophyTech(const Tech* previous)
    : Tech(3, previous)
{
    name = "Philosophy";
}

PhilosophyTech::PhilosophyTech()
    : Tech(3, &MeditationTech::getBase())
{
    name = "Philosophy";
}

const PhilosophyTech& PhilosophyTech::getBase() {
    static PhilosophyTech instance;
    return instance;
}