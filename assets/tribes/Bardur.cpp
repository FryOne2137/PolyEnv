//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#include "Bardur.h"
#include "techs/HuntingTech.h"

Bardur::Bardur()
    : Tribe(
        3,
        "Bardur",
        5,
        &HuntingTech::getBase()
    )
{
}