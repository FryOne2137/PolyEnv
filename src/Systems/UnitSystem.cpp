//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#include "UnitSystem.h"
//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#include "UnitSystem.h"

#include "Game.h"
#include "Unit.h"

#include <algorithm>

bool UnitSystem::unitExists(const Game& game, UnitId uid) {
    if (uid == kNoUnit) return false;
    return game.getUnit(uid) != nullptr;
}

PlayerId UnitSystem::getOwnerId(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getOwnerId() : kNoPlayer;
}

bool UnitSystem::setOwnerId(Game& game, UnitId uid, PlayerId pid) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setOwnerId(pid);
    return true;
}

UnitType UnitSystem::getType(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getType() : UnitType::Unknown;
}

bool UnitSystem::setType(Game& game, UnitId uid, UnitType t) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setType(t);
    return true;
}

int UnitSystem::getCost(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getCost() : 0;
}

bool UnitSystem::setCost(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setCost(v);
    return true;
}

bool UnitSystem::replaceUnit(Game& game, UnitId uid, const Unit& value) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    *u = value;
    return true;
}

Pos UnitSystem::getPos(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getPos() : Pos{-9999, -9999};
}

bool UnitSystem::setPos(Game& game, UnitId uid, Pos p) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setPos(p);
    return true;
}

int UnitSystem::getHealth(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getHealth() : 0;
}

int UnitSystem::getMaxHealth(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getMaxHealth() : 0;
}

bool UnitSystem::setHealth(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setHealth(v);
    return true;
}

bool UnitSystem::setMaxHealth(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setMaxHealth(v);
    return true;
}

bool UnitSystem::applyDamage(Game& game, UnitId uid, int dmg) {
    if (dmg <= 0) return true;
    Unit* u = game.getUnit(uid);
    if (!u) return false;

    const int next = std::max(0, u->getHealth() - dmg);
    u->setHealth(next);
    return true;
}

bool UnitSystem::heal(Game& game, UnitId uid, int amount) {
    if (amount <= 0) return true;
    Unit* u = game.getUnit(uid);
    if (!u) return false;

    const int next = std::min(u->getMaxHealth(), u->getHealth() + amount);
    u->setHealth(next);
    return true;
}

float UnitSystem::getAttack(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getAttack() : 0.0f;
}

float UnitSystem::getDefense(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getDefense() : 0.0f;
}

int UnitSystem::getMovePoints(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getMovePoints() : 0;
}

bool UnitSystem::setMovePoints(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setMovePoints(v);
    return true;
}

int UnitSystem::getRange(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getRange() : 1;
}

bool UnitSystem::setRange(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setRange(v);
    return true;
}

int UnitSystem::getVisionRange(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getVisionRange() : 0;
}

bool UnitSystem::setVisionRange(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setVisionRange(v);
    return true;
}

bool UnitSystem::movedThisTurn(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->movedThisTurn() : false;
}

bool UnitSystem::attackedThisTurn(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->attackedThisTurn() : false;
}

bool UnitSystem::setMovedThisTurn(Game& game, UnitId uid, bool v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setMovedThisTurn(v);
    return true;
}

bool UnitSystem::setAttackedThisTurn(Game& game, UnitId uid, bool v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setAttackedThisTurn(v);
    return true;
}

Pos UnitSystem::getLastMoveDir(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getLastMoveDir() : Pos{0, 0};
}

bool UnitSystem::setLastMoveDir(Game& game, UnitId uid, Pos d) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setLastMoveDir(d);
    return true;
}

Pos UnitSystem::getLastAttackDir(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getLastAttackDir() : Pos{0, 0};
}

bool UnitSystem::setLastAttackDir(Game& game, UnitId uid, Pos d) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setLastAttackDir(d);
    return true;
}

bool UnitSystem::isVeteran(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->isVeteran() : false;
}

bool UnitSystem::setVeteran(Game& game, UnitId uid, bool v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setVeteran(v);
    return true;
}

bool UnitSystem::isPoisoned(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getIsPoisoned() : false;
}

bool UnitSystem::setPoisoned(Game& game, UnitId uid, bool v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setPoisoned(v);
    return true;
}

int UnitSystem::getKillCounter(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getKillCounter() : 0;
}

bool UnitSystem::setKillCounter(Game& game, UnitId uid, int v) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setKillCounter(v);
    return true;
}

bool UnitSystem::addKill(Game& game, UnitId uid, int inc) {
    if (inc <= 0) return true;
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    for (int i = 0; i < inc; ++i) {
        u->addKill();
    }
    return true;
}

bool UnitSystem::hasSkill(const Game& game, UnitId uid, UnitSkill s) {
    const Unit* u = game.getUnit(uid);
    return u ? u->hasSkill(s) : false;
}

bool UnitSystem::addSkill(Game& game, UnitId uid, UnitSkill s) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->addSkill(s);
    return true;
}

bool UnitSystem::removeSkill(Game& game, UnitId uid, UnitSkill s) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->removeSkill(s);
    return true;
}

bool UnitSystem::isEmbarked(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->isEmbarked() : false;
}

UnitType UnitSystem::getEmbarkedBaseType(const Game& game, UnitId uid) {
    const Unit* u = game.getUnit(uid);
    return u ? u->getEmbarkedBaseType() : UnitType::Unknown;
}

bool UnitSystem::setEmbarkedBaseType(Game& game, UnitId uid, UnitType t) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->setEmbarkedBaseType(t);
    return true;
}

bool UnitSystem::clearEmbarkedBaseType(Game& game, UnitId uid) {
    Unit* u = game.getUnit(uid);
    if (!u) return false;
    u->clearEmbarkedBaseType();
    return true;
}

void UnitSystem::resetTurnState(Game& game, UnitId uid) {
    Unit* u = game.getUnit(uid);
    if (!u) return;
    u->setMovedThisTurn(false);
    u->setAttackedThisTurn(false);
}