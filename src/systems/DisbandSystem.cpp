//
// Created by Codex on 09/07/2026.
//

#include "DisbandSystem.h"

#include <algorithm>

#include "../game/Game.h"
#include "CitySystem.h"
#include "PlayerSystem.h"
#include "UnitSystem.h"
#include "../content/tech/TechDB.h"
#include "../content/units/UnitFactory.h"

int DisbandSystem::costForType(UnitType type) {
    switch (type) {
        case UnitType::Unknown:
            return 0;
        case UnitType::Giant:
        case UnitType::GiantSuper:
        case UnitType::Juggernaut:
            return 0;
        default:
            return std::max(0, UnitFactory::getUnitCost(type));
    }
}

int DisbandSystem::refundStars(const Game& game, UnitId unitId) {
    if (!UnitSystem::unitExists(game, unitId)) return 0;

    const UnitType type = UnitSystem::getType(game, unitId);
    if (type == UnitType::Giant || type == UnitType::GiantSuper || type == UnitType::Juggernaut) {
        return 5;
    }

    int totalCost = costForType(type);
    if (UnitSystem::isEmbarked(game, unitId)) {
        totalCost += costForType(UnitSystem::getEmbarkedBaseType(game, unitId));
    }

    return std::max(0, totalCost / 2);
}

bool DisbandSystem::canDisband(const Game& game, PlayerId pid, UnitId unitId) {
    if (game.isGameOver()) return false;
    if (pid == kNoPlayer) return false;
    if (!game.isPlayersTurn(pid)) return false;
    if (game.hasPendingCityUpgrade(pid)) return false;
    if (!PlayerSystem::playerExists(game, pid)) return false;
    if (!UnitSystem::unitExists(game, unitId)) return false;
    if (UnitSystem::getOwnerId(game, unitId) != pid) return false;
    if (!PlayerSystem::hasTech(game, pid, TechId::FreeSpirit)) return false;
    if (UnitSystem::movedThisTurn(game, unitId)) return false;
    if (UnitSystem::attackedThisTurn(game, unitId)) return false;
    return true;
}

bool DisbandSystem::disband(Game& game, PlayerId pid, UnitId unitId) {
    if (!canDisband(game, pid, unitId)) return false;

    const int refund = refundStars(game, unitId);
    const Pos pos = UnitSystem::getPos(game, unitId);

    if (game.getMap().inBounds(pos) && game.getMap().unitOn(pos) == unitId) {
        game.getMap().setUnitOn(pos, Map::kNoUnit);
    }

    CitySystem::removeUnitFromAnyCity(game, unitId);
    PlayerSystem::removeUnit(game, pid, unitId);

    (void)UnitSystem::setHealth(game, unitId, 0);
    (void)UnitSystem::setPos(game, unitId, Pos{-9999, -9999});
    (void)UnitSystem::setMovedThisTurn(game, unitId, true);
    (void)UnitSystem::setAttackedThisTurn(game, unitId, true);

    PlayerSystem::addStars(game, pid, refund);
    return true;
}
