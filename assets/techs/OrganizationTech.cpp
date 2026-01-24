#include "OrganizationTech.h"

OrganizationTech::OrganizationTech()
    : Tech(1, nullptr)
{
    name = "Organization";
}

const OrganizationTech& OrganizationTech::getBase() {
    static OrganizationTech instance;
    return instance;
}