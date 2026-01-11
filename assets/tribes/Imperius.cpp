//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Imperius.h"
#include "techs/OrganizationTech.h"

Imperius::Imperius()
    : Tribe(
        2,
        "Imperius",
        5,
        &OrganizationTech::getBase()
    )
{
}