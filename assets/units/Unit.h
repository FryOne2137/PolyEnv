//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#ifndef GAME_ENGINE_UNIT_H
#define GAME_ENGINE_UNIT_H

#include <vector>
#include <cstdint>
#include <../../src/Player/Player.h>
#include "../../src/world/Pos.h"
#include "UnitSkill.h"

class Unit {
public:
    ~Unit() = default;

    int getUnitId() const;

    bool moveTo(int x, int y);
    bool moveTo(Pos pos);

    std::vector<Pos> getReachablePositions() const;
    Pos getPosition() const;

    bool attackAt(Pos pos);
    bool attackAt(int x, int y);

    std::vector<Pos> getPossibleAttackAt() const;

    bool heal();
    bool makeVeteran();


private:
    int unitId;
    int kills;

    int x, y;

    int health;
    int maxHealth;
    int attack;
    int defense;
    bool veteran;
    int movement;
    int range;
    int cost;

    int visionRange;
    bool hasMovedThisTurn;
    bool hasAttackedThisTurn;

    uint64_t skillMask;

    const Player* owner;

    bool hasSkill(UnitSkill skill) const;

};

#endif // GAME_ENGINE_UNIT_H