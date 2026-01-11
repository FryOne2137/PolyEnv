//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Xinxi.h"
#include "techs/ClimbingTech.h"

Xinxi::Xinxi()
    : Tribe(
        1,
        "Xinxi",
        7,
        &ClimbingTech::getBase()
    )
{
}