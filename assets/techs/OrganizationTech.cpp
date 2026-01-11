//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "OrganizationTech.h"

OrganizationTech::OrganizationTech(const Tech* previous)
    : Tech(1, previous)
{
    name = "Organization";
}

OrganizationTech::OrganizationTech()
    : Tech(1, nullptr)
{
    name = "Organization";
}

const OrganizationTech& OrganizationTech::getBase() {
    static OrganizationTech instance;
    return instance;
}