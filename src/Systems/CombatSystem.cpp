//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "CombatSystem.h"

#include <algorithm>
#include <cstdlib>

bool CombatSystem::attack(Game& game, UnitId attackerId, Pos targetPos) {
    Unit* attacker = game.getUnit(attackerId);
    if (!attacker) return false;

    // One attack per turn for now
    if (attacker->attackedThisTurn()) return false;

    const Pos from = attacker->getPos();

    const int dx = std::abs(from.x - targetPos.x);
    const int dy = std::abs(from.y - targetPos.y);
    const int dist = std::max(dx, dy);

    // Range rule:
    // - adjacent target => dist == 1, requires range >= 1
    // - one tile gap (e.g. *_*) => dist == 2, requires range >= 2
    if (dist <= 0) return false;
    if (dist > attacker->getRange()) return false;

    // Must have an enemy unit on target
    const UnitId defenderId = game.getMap().unitOn(targetPos);
    if (defenderId == kNoUnit) return false;

    Unit* defender = game.getUnit(defenderId);
    if (!defender) return false;

    // Cannot attack own unit
    if (defender->getOwnerId() == attacker->getOwnerId()) return false;

    // Simple damage (no counterattack, no terrain, no veteran bonuses yet)
    const int raw = static_cast<int>(attacker->getAttack() - defender->getDefense());
    const int damage = std::max(1, raw);

    defender->setHealth(defender->getHealth() - damage);
    attacker->setAttackedThisTurn(true);

    if (defender->getHealth() <= 0) {
        // Remove defender from map occupancy
        game.getMap().setUnitOn(targetPos, kNoUnit);

        // Remove defender from owner's list
        game.getPlayer(defender->getOwnerId()).removeUnit(defenderId);

        // Optional: reward kill counter
        attacker->addKill();
    }

    return true;
}