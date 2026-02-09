//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#include "UnitFactory.h"
#include "Unit.h"
#include "systems/GameDataSystem.h"
#include <algorithm>


Unit UnitFactory::create(UnitType type, PlayerId ownerId, Pos pos) {
    Unit u;
    u.setType(type);
    u.setOwnerId(ownerId);
    u.setPos(pos);

    GameDataSystem::applyUnitTemplate(u);

    u.setMovedThisTurn(false);
    u.setAttackedThisTurn(false);

    return u;
}

int UnitFactory::getUnitCost(UnitType type) {
    return GameDataSystem::getUnitCost(type);
}


void UnitFactory::applyBaseStats(Unit& u) {
    // All base stats/skills are now driven by Units.json via GameDataSystem.
    GameDataSystem::applyUnitTemplate(u);
}