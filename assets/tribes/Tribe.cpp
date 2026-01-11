//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#include "Tribe.h"

Tribe::Tribe(int id, std::string name, int stars, const Tech* tech)
    : tribeId(id), tribeName(std::move(name)), startStars(stars), startTech(tech) {}


int Tribe::getId() {
    return tribeId;
}

const std::string& Tribe::getName() const {
    return tribeName;
}

int Tribe::getStartStars() {
    return startStars;
}

const Tech* Tribe::getStartTech() const {
    return startTech;
}