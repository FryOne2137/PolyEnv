//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#ifndef GAME_ENGINE_UNITSYSTEM_H
#define GAME_ENGINE_UNITSYSTEM_H

#include "Core/Ids.h"
#include "World/Pos.h"
#include "skills/UnitSkill.h"
#include "Unit.h"

class Game;

// UnitSystem is the ONLY allowed way for other systems to access or modify Unit state.
// Never use game.getUnit(...)-> directly outside this system.
class UnitSystem {
public:

    // ---- Existence ----
    static bool unitExists(const Game& game, UnitId uid);

    // ---- Owner / type ----
    static PlayerId getOwnerId(const Game& game, UnitId uid);
    static bool setOwnerId(Game& game, UnitId uid, PlayerId pid);

    static UnitType getType(const Game& game, UnitId uid);
    static bool setType(Game& game, UnitId uid, UnitType type);

    static int getCost(const Game &game, UnitId uid);

    static bool setCost(Game &game, UnitId uid, int v);

    // ---- Position ----
    static Pos getPos(const Game& game, UnitId uid);
    static bool setPos(Game& game, UnitId uid, Pos pos);

    static bool replaceUnit(Game& game, UnitId uid, const Unit& value);

    // ---- Health ----
    static int getHealth(const Game& game, UnitId uid);
    static int getMaxHealth(const Game& game, UnitId uid);
    static bool setHealth(Game& game, UnitId uid, int value);
    static bool setMaxHealth(Game& game, UnitId uid, int value);

    static bool applyDamage(Game& game, UnitId uid, int dmg);
    static bool heal(Game& game, UnitId uid, int amount);

    // ---- Combat stats ----
    static float getAttack(const Game& game, UnitId uid);
    static float getDefense(const Game& game, UnitId uid);

    // ---- Movement ----
    static int getMovePoints(const Game& game, UnitId uid);
    static bool setMovePoints(Game& game, UnitId uid, int value);

    static int getRange(const Game& game, UnitId uid);
    static bool setRange(Game& game, UnitId uid, int value);

    static int getVisionRange(const Game& game, UnitId uid);
    static bool setVisionRange(Game& game, UnitId uid, int value);

    // ---- Turn state ----
    static bool movedThisTurn(const Game& game, UnitId uid);
    static bool attackedThisTurn(const Game& game, UnitId uid);

    static bool setMovedThisTurn(Game& game, UnitId uid, bool value);
    static bool setAttackedThisTurn(Game& game, UnitId uid, bool value);

    static void resetTurnState(Game& game, UnitId uid);

    // ---- Direction tracking ----
    static Pos getLastMoveDir(const Game& game, UnitId uid);
    static bool setLastMoveDir(Game& game, UnitId uid, Pos dir);

    static Pos getLastAttackDir(const Game& game, UnitId uid);
    static bool setLastAttackDir(Game& game, UnitId uid, Pos dir);

    // ---- Status ----
    static bool isVeteran(const Game& game, UnitId uid);
    static bool setVeteran(Game& game, UnitId uid, bool value);

    static bool isPoisoned(const Game& game, UnitId uid);
    static bool setPoisoned(Game& game, UnitId uid, bool value);

    // ---- Kills ----
    static int getKillCounter(const Game& game, UnitId uid);
    static bool setKillCounter(Game& game, UnitId uid, int value);
    static bool addKill(Game& game, UnitId uid, int amount = 1);

    // ---- Skills ----
    static bool hasSkill(const Game& game, UnitId uid, UnitSkill skill);
    static bool addSkill(Game& game, UnitId uid, UnitSkill skill);
    static bool removeSkill(Game& game, UnitId uid, UnitSkill skill);

    // ---- Embark ----
    static bool isEmbarked(const Game& game, UnitId uid);
    static UnitType getEmbarkedBaseType(const Game& game, UnitId uid);

    static bool setEmbarkedBaseType(Game& game, UnitId uid, UnitType type);
    static bool clearEmbarkedBaseType(Game& game, UnitId uid);
};

#endif //GAME_ENGINE_UNITSYSTEM_H