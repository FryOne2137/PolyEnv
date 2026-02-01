//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "CombatSystem.h"

#include "Systems/MovementSystem.h"

#include "Game.h"
#include "terrain/SettlementTypeEnum.h"
#include "City.h"
#include "skills/UnitSkill.h"

#include <algorithm>
#include <cmath>
#include <vector>

static inline int iround(double v) {
    return static_cast<int>(std::llround(v));
}

int CombatSystem::chebyshevDistance(Pos a, Pos b) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    return std::max(dx, dy);
}

static double computeDefenceBonus(const Game& game, const Unit& defender) {
    // Default: no bonus
    double bonus = 1.0;

    const Pos p = defender.getPos();
    const Tile& t = game.getMap().at(p);

    const PlayerId owner = defender.getOwnerId();
    const Player& pl = game.getPlayer(owner);

    // Use correct terrain/resource checks for defence bonus
    const auto base = t.getBaseTerrain();
    const auto res = t.getResource();

    // Defence bonus unlocks (per the table):
    // Forest(resource) -> Archery, Mountain(base) -> Climbing,
    // Water(base, shallow) -> Aquatism, Ocean(base) -> Aquatism
    if (res == ResourcesEnum::Forest) {
        if (pl.hasTech(TechId::Archery)) bonus = 1.5;
    } else if (base == BaseTerrainEnum::Mountain) {
        if (pl.hasTech(TechId::Climbing)) bonus = 1.5;
    } else if (base == BaseTerrainEnum::Water) {
        if (pl.hasTech(TechId::Aquatism)) bonus = 1.5;
    } else if (base == BaseTerrainEnum::Ocean) {
        if (pl.hasTech(TechId::Aquatism)) bonus = 1.5;
    }

    // --- City / Fortify / Wall defence bonus rules ---
    // Unit must stand in a CITY tile
    double cityBonus = 1.0;
    if (t.getSettlementType() == SettlementTypeEnum::City) {
        // Units without Fortify get NO city defence bonus
        if (defender.hasSkill(UnitSkill::Fortify)) {
            cityBonus = 1.5;

            // City wall adds +2.5 => total 4.0
            if (const City* c = game.getCityBySettlementId(t.getSettlementId())) {
                if (c->hasCityWallEnabled()) {
                    cityBonus += 2.5; // 1.5 + 2.5 = 4.0
                }
            }
        }
    }

    // City bonus overrides terrain bonus if larger
    if (cityBonus > bonus) {
        bonus = cityBonus;
    }

    // Poison modifiers from the wiki (optional, but you already have poisoned flag on Unit)
    // If poisoned, the wiki states defenceBonus values are reduced:
    //   none: 0.5, standard: 0.7, wall: 2
    if (defender.getIsPoisoned()) {
        if (bonus <= 1.0) {
            bonus = 0.5;
        } else if (bonus <= 1.5) {
            bonus = 0.7;
        } else {
            bonus = 2.0;
        }
    }

    return bonus;
}

// Kill helper lives in a SYSTEM (not in Game public API).
static void killUnit(Game& game, UnitId uid) {
    Unit* u = game.getUnit(uid);
    if (!u) return;

    const Pos p = u->getPos();

    // 1) Clear map occupancy
    if (game.getMap().inBounds(p) && game.getMap().unitOn(p) == uid) {
        game.getMap().setUnitOn(p, Map::kNoUnit);
    }

    const PlayerId owner = u->getOwnerId();
    if (owner != kNoPlayer) {
        game.getPlayer(owner).removeUnit(uid);
    }

    u->setHealth(0);
    u->setPos(Pos{-9999, -9999});
    u->setMovedThisTurn(true);
    u->setAttackedThisTurn(true);
}

std::vector<Pos> CombatSystem::attackable(const Game& game, UnitId attackerId) {
    std::vector<Pos> out;

    const Unit* attacker = game.getUnit(attackerId);
    if (!attacker) return out;

    if (attacker->attackedThisTurn()) return out;

    // After moving, attacking is only allowed for units with Dash.
    if (attacker->movedThisTurn() && !attacker->hasSkill(UnitSkill::Dash)) return out;

    const PlayerId attackerOwner = attacker->getOwnerId();
    const Pos from = attacker->getPos();
    const int range = attacker->getRange();

    // Scan tiles within Chebyshev radius (same metric as `attack()`).
    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) > range) continue;

            const Pos p{from.x + dx, from.y + dy};
            if (!game.getMap().inBounds(p)) continue;

            const UnitId uid = game.getMap().unitOn(p);
            if (uid == Map::kNoUnit) continue;

            const Unit* target = game.getUnit(uid);
            if (!target) continue;

            // Only enemy units.
            if (target->getOwnerId() == attackerOwner) continue;

            out.push_back(p);
        }
    }

    return out;
}

bool CombatSystem::attack(Game& game, UnitId attackerId, Pos targetPos) {
    Unit* attacker = game.getUnit(attackerId);
    if (!attacker) return false;

    if (!game.getMap().inBounds(targetPos)) return false;

    const UnitId defenderId = game.getMap().unitOn(targetPos);
    if (defenderId == Map::kNoUnit) return false;

    Unit* defender = game.getUnit(defenderId);
    if (!defender) return false;

    // Can only attack other players
    if (defender->getOwnerId() == attacker->getOwnerId()) return false;

    const Pos attackerPos = attacker->getPos();
    const int dist = chebyshevDistance(attackerPos, targetPos);

    // Range check
    const int attackerRange = attacker->getRange();
    if (dist > attackerRange) return false;

    // If attacker already attacked this turn
    if (attacker->attackedThisTurn()) return false;

    // After moving, attacking is only allowed for units with Dash
    if (attacker->movedThisTurn() && !attacker->hasSkill(UnitSkill::Dash)) return false;

    // --- Polytopia damage formula ---
    const double aAtk = static_cast<double>(attacker->getAttack());
    const double dDef = static_cast<double>(defender->getDefense());

    const double aHp = static_cast<double>(attacker->getHealth());
    const double aMaxHp = static_cast<double>(attacker->getMaxHealth());
    const double dHp = static_cast<double>(defender->getHealth());
    const double dMaxHp = static_cast<double>(defender->getMaxHealth());

    if (aMaxHp <= 0.0 || dMaxHp <= 0.0) return false;

    const double attackForce = aAtk * (aHp / aMaxHp);
    const double defenceBonus = computeDefenceBonus(game, *defender);
    const double defenceForce = dDef * (dHp / dMaxHp) * defenceBonus;

    const double total = std::max(1e-9, attackForce + defenceForce);

    const int attackResult = std::max(0, iround((attackForce / total) * aAtk * 4.5));
    const int defenceResult = std::max(0, iround((defenceForce / total) * dDef * 4.5));

    // Apply damage to defender
    defender->setHealth(std::max(0, defender->getHealth() - attackResult));

    // --- Splash (Polytopia Bomber-style) ---
    // If attacker has Splash, deal half of the main attack damage (rounded down)
    // to all ENEMY units adjacent to the target (8-neighborhood).
    if (attacker->hasSkill(UnitSkill::Splash)) {
        const int splashDamage = std::max(0, attackResult / 2); // half, rounded down
        if (splashDamage > 0) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue; // skip the target tile itself

                    const Pos p{targetPos.x + dx, targetPos.y + dy};
                    if (!game.getMap().inBounds(p)) continue;

                    const UnitId sid = game.getMap().unitOn(p);
                    if (sid == Map::kNoUnit) continue;

                    Unit* su = game.getUnit(sid);
                    if (!su) continue;

                    // Only enemy units take splash.
                    if (su->getOwnerId() == attacker->getOwnerId()) continue;

                    su->setHealth(std::max(0, su->getHealth() - splashDamage));
                    if (su->getHealth() <= 0) {
                        attacker->addKill();
                        killUnit(game, sid);
                    }
                }
            }
        }
    }

    // We set attackedThisTurn at the END of this function.
    // This lets a melee unit advance into the killed unit's tile using MovementSystem::move
    // without being blocked by the post-attack flag.
    bool attackedThisTurnFinal = true;

    // If defender died, no retaliation
    if (defender->getHealth() <= 0) {
        attacker->addKill();

        // Kill first (clears map occupancy on targetPos)
        killUnit(game, defenderId);

        // PERSIST: if the attacker has Persist, it may keep attacking this turn
        // (but only when it killed a unit)
        attackedThisTurnFinal = !attacker->hasSkill(UnitSkill::Persist);

        // Melee (range==1) advances into the killed unit's tile, unless territory rules forbid it.
        if (attacker->getRange() == 1) {
            const bool oldMoved = attacker->movedThisTurn();

            attacker->setMovedThisTurn(false);
            attacker->setAttackedThisTurn(false);

            const bool moved = MovementSystem::move(game, attackerId, targetPos);

            if (!moved) {
                attacker->setMovedThisTurn(oldMoved);
            }
        }
        // Finalize attack flags
        attacker->setAttackedThisTurn(attackedThisTurnFinal);

        // Remember last attack direction (for forced spawn push logic)
        {
            const Pos from = attackerPos;
            const Pos to   = targetPos;
            attacker->setLastAttackDir(Pos{to.x - from.x, to.y - from.y});
        }

        // Escape: after attacking, unit may move again this turn (including after the post-kill advance)
        if (attacker->hasSkill(UnitSkill::Escape)) {
            attacker->setMovedThisTurn(false);
        }

        return true;
    }

    // Retaliation only if defender can reach attacker
    const int defenderRange = defender->getRange();
    const int backDist = chebyshevDistance(defender->getPos(), attackerPos);

    // Retaliation is blocked if defender has the Stiff skill
    if (defenceResult > 0 && backDist <= defenderRange && !defender->hasSkill(UnitSkill::Stiff)) {
        attacker->setHealth(std::max(0, attacker->getHealth() - defenceResult));
        if (attacker->getHealth() <= 0) {
            defender->addKill();
            killUnit(game, attackerId);
        }
    }

    // Remember last attack direction (for forced spawn push logic)
    {
        const Pos from = attackerPos;
        const Pos to   = targetPos;
        attacker->setLastAttackDir(Pos{to.x - from.x, to.y - from.y});
    }

    // Finalize attack flags
    attacker->setAttackedThisTurn(true);

    // Escape: after attacking, unit may move again this turn
    if (attacker->hasSkill(UnitSkill::Escape)) {
        attacker->setMovedThisTurn(false);
    }

    return true;
}
