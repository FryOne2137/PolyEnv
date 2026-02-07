//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "CombatSystem.h"
#include "Systems/CitySystem.h"
#include "Systems/UnitSystem.h"
#include "Systems/PlayerSystem.h"

#include "Systems/MovementSystem.h"
#include "Systems/UnitSpawnSystem.h"
#include "Systems/MonumentSystem.h"
#include "Systems/InfiltrationSystem.h"
#include "Game.h"
#include "terrain/SettlementTypeEnum.h"
#include "City.h"
#include "skills/UnitSkill.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <iostream>

static inline int iround(double v) {
    return static_cast<int>(std::llround(v));
}

int CombatSystem::chebyshevDistance(Pos a, Pos b) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    return std::max(dx, dy);
}

static double computeDefenceBonus(const Game& game, UnitId defenderId) {
    // Default: no bonus
    double bonus = 1.0;

    const Pos p = UnitSystem::getPos(game, defenderId);
    const Tile& t = game.getMap().at(p);

    const PlayerId owner = UnitSystem::getOwnerId(game, defenderId);

    // Use correct terrain/resource checks for defence bonus
    const auto base = t.getBaseTerrain();
    const auto res = t.getResource();

    // Defence bonus unlocks (per the table):
    // Forest(resource) -> Archery, Mountain(base) -> Climbing,
    // Water(base, shallow) -> Aquatism, Ocean(base) -> Aquatism
    if (res == ResourcesEnum::Forest) {
        if (PlayerSystem::hasTech(game, owner, TechId::Archery)) bonus = 1.5;
    } else if (base == BaseTerrainEnum::Mountain) {
        if (PlayerSystem::hasTech(game, owner, TechId::Climbing)) bonus = 1.5;
    } else if (base == BaseTerrainEnum::Water) {
        if (PlayerSystem::hasTech(game, owner, TechId::Aquatism)) bonus = 1.5;
    } else if (base == BaseTerrainEnum::Ocean) {
        if (PlayerSystem::hasTech(game, owner, TechId::Aquatism)) bonus = 1.5;
    }

    // --- City / Fortify / Wall defence bonus rules ---
    // Unit must stand in a CITY tile
    double cityBonus = 1.0;
    if (t.getSettlementType() == SettlementTypeEnum::City) {
        // Units without Fortify get NO city defence bonus
        if (UnitSystem::hasSkill(game, defenderId, UnitSkill::Fortify)) {
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
    if (UnitSystem::isPoisoned(game, defenderId)) {
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

static inline void awardKillToPlayer(Game& game, PlayerId pid, UnitId killerId, int kills = 1) {
    if (kills <= 0) return;

    const PlayerId owner = UnitSystem::getOwnerId(game, killerId);
    if (owner == kNoPlayer) return;

    // Safety: only award if the caller-specified player matches the unit owner
    // and it is currently that player's turn.
    if (pid != owner) return;

    PlayerSystem::addKill(game, owner, kills);

    // After updating total kills, check monument unlock condition.
    MonumentSystem::onKillsUpdated(game, owner, static_cast<int>(PlayerSystem::getKillerCount(game, owner)));
}


// Kill helper lives in a SYSTEM (not in Game public API).
static void killUnit(Game& game, UnitId uid) {
    if (!UnitSystem::unitExists(game, uid)) return;

    const Pos p = UnitSystem::getPos(game, uid);

    // 1) Clear map occupancy
    if (game.getMap().inBounds(p) && game.getMap().unitOn(p) == uid) {
        game.getMap().setUnitOn(p, Map::kNoUnit);
    }

    const PlayerId owner = UnitSystem::getOwnerId(game, uid);
    if (owner != kNoPlayer) {
        PlayerSystem::removeUnit(game, owner, uid);
    }

    // NEW: always free city capacity slot for this unit, even if it died outside a city tile
    CitySystem::removeUnitFromAnyCity(game, uid);

    // 2) If the unit is on a city tile, remove it from that City's unit list via CitiesSystem.
    if (game.getMap().inBounds(p)) {
        const Tile& t = game.getMap().at(p);
        if (t.getSettlementType() == SettlementTypeEnum::City) {
            if (City* c = game.getCityBySettlementId(t.getSettlementId())) {
                CitySystem::removeUnitFromCity(game, uid, c->getCityId());
            }
        }
    }

    UnitSystem::setHealth(game, uid, 0);
    UnitSystem::setPos(game, uid, Pos{-9999, -9999});
    UnitSystem::setMovedThisTurn(game, uid, true);
    UnitSystem::setAttackedThisTurn(game, uid, true);
}


std::vector<Pos> CombatSystem::attackable(const Game& game, UnitId attackerId) {
    std::vector<Pos> out;

    if (!UnitSystem::unitExists(game, attackerId)) return out;

    if (UnitSystem::attackedThisTurn(game, attackerId)) return out;

    // After moving, attacking is only allowed for units with Dash.
    if (UnitSystem::movedThisTurn(game, attackerId) && !UnitSystem::hasSkill(game, attackerId, UnitSkill::Dash)) return out;

    const PlayerId attackerOwner = UnitSystem::getOwnerId(game, attackerId);
    const Pos from = UnitSystem::getPos(game, attackerId);
    const int range = UnitSystem::getRange(game, attackerId);

    // Scan tiles within Chebyshev radius (same metric as `attack()`).
    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) > range) continue;

            const Pos p{from.x + dx, from.y + dy};
            if (!game.getMap().inBounds(p)) continue;

            const UnitId uid = game.getMap().unitOn(p);
            if (uid == Map::kNoUnit) continue;

            if (!UnitSystem::unitExists(game, uid)) continue;

            // Only enemy units.
            if (UnitSystem::getOwnerId(game, uid) == attackerOwner) continue;

            out.push_back(p);
        }
    }

    return out;
}

bool CombatSystem::attack(Game& game, UnitId attackerId, Pos targetPos) {
    if (!UnitSystem::unitExists(game, attackerId)) return false;

    if (!game.getMap().inBounds(targetPos)) return false;
    const Pos attackerPos = UnitSystem::getPos(game, attackerId);

    // --- Infiltrate (Cloak / Dinghy / Scuba etc.) ---
    // Infiltrate targets an adjacent ENEMY CITY tile (may be empty). It consumes the infiltrator.
    if (UnitSystem::hasSkill(game, attackerId, UnitSkill::Infiltrate)) {
        const PlayerId infiltrator = UnitSystem::getOwnerId(game, attackerId);
        if (infiltrator == kNoPlayer) return false;

        const int dist2 = chebyshevDistance(attackerPos, targetPos);
        if (dist2 != 1) return false;

        const Tile& tt = game.getMap().at(targetPos);
        if (tt.getSettlementType() != SettlementTypeEnum::City) return false;

        const CityId cityId = static_cast<CityId>(tt.getSettlementId());
        if (!CitySystem::cityExists(game, cityId)) return false;

        const PlayerId cityOwner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId));
        if (cityOwner == kNoPlayer) return false;
        if (cityOwner == infiltrator) return false;

        return InfiltrationSystem::infiltrateCity(game, attackerId, cityId);
    }

    // From here on, we are attacking a UNIT on the target tile.
    const UnitId defenderId = game.getMap().unitOn(targetPos);
    if (defenderId == Map::kNoUnit) return false;

    if (!UnitSystem::unitExists(game, defenderId)) return false;

    // Can only attack other players
    if (UnitSystem::getOwnerId(game, defenderId) == UnitSystem::getOwnerId(game, attackerId)) return false;
    // --- Polytopia damage formula ---
    const double aAtk = static_cast<double>(UnitSystem::getAttack(game, attackerId));
    const double dDef = static_cast<double>(UnitSystem::getDefense(game, defenderId));

    const double aHp = static_cast<double>(UnitSystem::getHealth(game, attackerId));
    const double aMaxHp = static_cast<double>(UnitSystem::getMaxHealth(game, attackerId));
    const double dHp = static_cast<double>(UnitSystem::getHealth(game, defenderId));
    const double dMaxHp = static_cast<double>(UnitSystem::getMaxHealth(game, defenderId));

    if (aMaxHp <= 0.0 || dMaxHp <= 0.0) return false;

    const double attackForce = aAtk * (aHp / aMaxHp);
    const double defenceBonus = computeDefenceBonus(game, defenderId);
    const double defenceForce = dDef * (dHp / dMaxHp) * defenceBonus;

    const double total = std::max(1e-9, attackForce + defenceForce);

    const int attackResult = std::max(0, iround((attackForce / total) * aAtk * 4.5));
    const int defenceResult = std::max(0, iround((defenceForce / total) * dDef * 4.5));

    // Apply damage to defender
    UnitSystem::setHealth(game, defenderId, std::max(0, UnitSystem::getHealth(game, defenderId) - attackResult));

    // --- Splash (Polytopia Bomber-style) ---
    // If attacker has Splash, deal half of the main attack damage (rounded down)
    // to all ENEMY units adjacent to the target (8-neighborhood).
    if (UnitSystem::hasSkill(game, attackerId, UnitSkill::Splash)) {
        const int splashDamage = std::max(0, attackResult / 2); // half, rounded down
        if (splashDamage > 0) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue; // skip the target tile itself

                    const Pos p{targetPos.x + dx, targetPos.y + dy};
                    if (!game.getMap().inBounds(p)) continue;

                    const UnitId sid = game.getMap().unitOn(p);
                    if (sid == Map::kNoUnit) continue;

                    if (!UnitSystem::unitExists(game, sid)) continue;

                    // Only enemy units take splash.
                    if (UnitSystem::getOwnerId(game, sid) == UnitSystem::getOwnerId(game, attackerId)) continue;

                    UnitSystem::setHealth(game, sid, std::max(0, UnitSystem::getHealth(game, sid) - splashDamage));
                    if (UnitSystem::getHealth(game, sid) <= 0) {
                        UnitSystem::addKill(game, attackerId);
                        awardKillToPlayer(game, UnitSystem::getOwnerId(game, attackerId), attackerId);
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
    if (UnitSystem::getHealth(game, defenderId) <= 0) {
        UnitSystem::addKill(game, attackerId);
        awardKillToPlayer(game, UnitSystem::getOwnerId(game, attackerId), attackerId);

        // Kill first (clears map occupancy on targetPos)
        killUnit(game, defenderId);

        // PERSIST: if the attacker has Persist, it may keep attacking this turn
        // (but only when it killed a unit)
        attackedThisTurnFinal = !UnitSystem::hasSkill(game, attackerId, UnitSkill::Persist);

        // Melee (range==1) advances into the killed unit's tile, unless territory rules forbid it.
        if (UnitSystem::getRange(game, attackerId) == 1) {
            const bool oldMoved = UnitSystem::movedThisTurn(game, attackerId);

            UnitSystem::setMovedThisTurn(game, attackerId, false);
            UnitSystem::setAttackedThisTurn(game, attackerId, false);

            const bool moved = MovementSystem::move(game, attackerId, targetPos);

            if (!moved) {
                UnitSystem::setMovedThisTurn(game, attackerId, oldMoved);
            }
        }
        // Finalize attack flags
        UnitSystem::setAttackedThisTurn(game, attackerId, attackedThisTurnFinal);

        // Remember last attack direction (for forced spawn push logic)
        {
            const Pos from = attackerPos;
            const Pos to   = targetPos;
            UnitSystem::setLastAttackDir(game, attackerId, Pos{to.x - from.x, to.y - from.y});
        }

        // Escape: after attacking, unit may move again this turn (including after the post-kill advance)
        if (UnitSystem::hasSkill(game, attackerId, UnitSkill::Escape)) {
            UnitSystem::setMovedThisTurn(game, attackerId, false);
        }

        return true;
    }

    // Retaliation only if defender can reach attacker
    const int defenderRange = UnitSystem::getRange(game, defenderId);
    const int backDist = chebyshevDistance(UnitSystem::getPos(game, defenderId), attackerPos);

    // Retaliation is blocked if defender has the Stiff skill
    if (defenceResult > 0 && backDist <= defenderRange && !UnitSystem::hasSkill(game, defenderId, UnitSkill::Stiff)) {
        UnitSystem::setHealth(game, attackerId, std::max(0, UnitSystem::getHealth(game, attackerId) - defenceResult));
        if (UnitSystem::getHealth(game, attackerId) <= 0) {
            UnitSystem::addKill(game, defenderId);
            awardKillToPlayer(game, UnitSystem::getOwnerId(game, defenderId), defenderId);
            killUnit(game, attackerId);
        }
    }

    // Remember last attack direction (for forced spawn push logic)
    {
        const Pos from = attackerPos;
        const Pos to   = targetPos;
        UnitSystem::setLastAttackDir(game, attackerId, Pos{to.x - from.x, to.y - from.y});
    }

    // Finalize attack flags
    UnitSystem::setAttackedThisTurn(game, attackerId, true);

    // Escape: after attacking, unit may move again this turn
    if (UnitSystem::hasSkill(game, attackerId, UnitSkill::Escape)) {
        UnitSystem::setMovedThisTurn(game, attackerId, false);
    }

    // Ensure monument progress is updated after any attack resolution
    {
        const PlayerId owner = UnitSystem::getOwnerId(game, attackerId);
        if (owner != kNoPlayer) {
            MonumentSystem::onKillsUpdated(
                game,
                owner,
                static_cast<int>(PlayerSystem::getKillerCount(game, owner))
            );
        }
    }

    return true;
}


bool CombatSystem::heal(Game& game, UnitId healerId) {
    if (!UnitSystem::unitExists(game, healerId)) return false;

    // Only units with the Heal skill can use Heal Others.
    if (!UnitSystem::hasSkill(game, healerId, UnitSkill::Heal)) return false;

    // Cannot heal if already used attack/action this turn.
    if (UnitSystem::attackedThisTurn(game, healerId)) return false;

    // Same rule as attack: after moving, action is only allowed for units with Dash.
    if (UnitSystem::movedThisTurn(game, healerId) && !UnitSystem::hasSkill(game, healerId, UnitSkill::Dash)) {
        return false;
    }

    const PlayerId owner = UnitSystem::getOwnerId(game, healerId);
    if (owner == kNoPlayer) return false;

    const Pos center = UnitSystem::getPos(game, healerId);
    if (!game.getMap().inBounds(center)) return false;

    bool didHeal = false;

    // Heal all adjacent (8-neighborhood) friendly units by up to 4 HP.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;

            const Pos p{center.x + dx, center.y + dy};
            if (!game.getMap().inBounds(p)) continue;

            const UnitId uid = game.getMap().unitOn(p);
            if (uid == Map::kNoUnit) continue;
            if (!UnitSystem::unitExists(game, uid)) continue;

            if (UnitSystem::getOwnerId(game, uid) != owner) continue;

            const int before = UnitSystem::getHealth(game, uid);
            if (before <= 0) continue;

            // Wiki: Heal Others heals by up to 4 HP (not above max HP).
            (void)UnitSystem::heal(game, uid, 4);

            const int after = UnitSystem::getHealth(game, uid);
            if (after > before) {
                didHeal = true;
            }
        }
    }

    // If nothing was healed, do not consume the action.
    if (!didHeal) return false;

    // Consume the unit's action for this turn: block both moving and attacking.
    UnitSystem::setAttackedThisTurn(game, healerId, true);
    UnitSystem::setMovedThisTurn(game, healerId, true);

    return true;
}