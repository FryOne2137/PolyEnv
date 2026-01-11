//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Tech.h"
#include <ostream>

bool Tech::hasPrevious() const {
    return previousTech != nullptr;
}

const Tech* Tech::getPrevious() const {
    return previousTech;
}

int Tech::getTier() const {
    return tier;
}

int Tech::getPrice(int numOfCities) const {
    return numOfCities*Tech::getTier()+4;
}

std::string Tech::getName() const {
    return name;
}

Tech::Tech(int tier, const Tech* previous)
    : tier(tier), previousTech(previous) {}
