//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#include "Unit.h"

// ---- Identity / ownership ----
UnitId Unit::getId() const { return id; }
void Unit::setId(UnitId v) { id = v; }

PlayerId Unit::getOwnerId() const { return ownerId; }
void Unit::setOwnerId(PlayerId v) { ownerId = v; }

UnitType Unit::getType() const { return type; }
void Unit::setType(UnitType v) { type = v; }

// ---- Position ----
Pos Unit::getPos() const { return pos; }
void Unit::setPos(Pos p) { pos = p; }

// ---- Stats ----
int Unit::getHealth() const { return health; }
int Unit::getMaxHealth() const { return maxHealth; }
void Unit::setHealth(int v) { health = v; }
void Unit::setMaxHealth(int v) { maxHealth = v; }

float Unit::getAttack() const { return attack; }
float Unit::getDefense() const { return defense; }
void Unit::setDefense(float v) { defense = v; }

int Unit::getMovePoints() const { return movePoints; }
void Unit::setMovePoints(int v) { movePoints = v; }

int Unit::getRange() const { return range; }
void Unit::setRange(int v) { range = v; }

int Unit::getCost() const { return cost; }
void Unit::setCost(int v) { cost = v; }

int Unit::getVisionRange() const { return visionRange; }
void Unit::setVisionRange(int v) { visionRange = v; }

// ---- Turn state ----
bool Unit::movedThisTurn() const { return hasMovedThisTurn; }
bool Unit::attackedThisTurn() const { return hasAttackedThisTurn; }
void Unit::setMovedThisTurn(bool v) { hasMovedThisTurn = v; }
void Unit::setAttackedThisTurn(bool v) { hasAttackedThisTurn = v; }

TechId Unit::getRequiredTechToSpawn() const {
    return requiredTechToSpawn;
}
void Unit::setRequiredTechToSpawn(TechId v) {
    requiredTechToSpawn=v;
}


// ---- Status ----
bool Unit::isVeteran() const { return veteran; }
void Unit::setVeteran(bool v) { veteran = v; }

bool Unit::poisoned() const { return isPoisoned; }
void Unit::setPoisoned(bool v) { isPoisoned = v; }

void Unit::setAttack(float v){attack=v;};


int Unit::getKillCounter() const { return killCounter; }
void Unit::addKill() { ++killCounter; }

Pos Unit::getLastMoveDir() const {
    return lastMoveDir;
}

void Unit::setLastMoveDir(Pos d) {
    lastMoveDir = d;
}

Pos Unit::getLastAttackDir() const {
    return lastAttackDir;
}

void Unit::setLastAttackDir(Pos d) {
    lastAttackDir = d;
}

// ---- Skills (bitmask) ----
bool Unit::hasSkill(UnitSkill skill) const {
    return (skillMask & static_cast<uint64_t>(skill)) != 0ULL;
}

void Unit::addSkill(UnitSkill skill) {
    skillMask |= static_cast<uint64_t>(skill);
}

void Unit::removeSkill(UnitSkill skill) {
    skillMask &= ~static_cast<uint64_t>(skill);
}

// ---- Embark / disembark ----
bool Unit::isEmbarked() const {
    return embarkedBaseType != UnitType::Unknown;
}

UnitType Unit::getEmbarkedBaseType() const {
    return embarkedBaseType;
}

void Unit::setEmbarkedBaseType(UnitType v) {
    embarkedBaseType = v;
}

void Unit::clearEmbarkedBaseType() {
    embarkedBaseType = UnitType::Unknown;
}
