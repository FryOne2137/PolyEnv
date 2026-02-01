//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#ifndef GAME_ENGINE_UNIT_H
#define GAME_ENGINE_UNIT_H

#include <cstdint>

#include "../../src/World/Pos.h"
#include "UnitSkill.h"
#include "Core/Ids.h"
#include "tech/TechDB.h"

// In this architecture Unit is "data only". Game rules live in systems:
// - MovementSystem (movement validation, reachable tiles, fog reveal)
// - CombatSystem   (attacks, damage, counterattacks)
// - VisionSystem   (visibility updates)
// Unit does not depend on Map/World/Player headers.


enum class UnitType : uint8_t {
    Unknown = 0,

    // Land Units
    Warrior    = 1,
    Archer     = 2,
    Defender   = 3,
    Rider      = 4,
    MindBender = 5,
    Swordsman  = 6,
    Catapult   = 7,
    Cloak      = 8,
    Knight     = 9,
    Giant      = 10,
    Bunny      = 11,
    Bunta      = 12,

    // Naval Units
    Raft       = 20,
    Scout      = 21,
    Rammer     = 22,
    Bomber     = 23,
    Dinghy     = 24,
    Pirate     = 25,
    Juggernaut = 26,

    // Aquarion Tribe Units
    Mermaid    = 30,
    AquaticAmphibian = 31,
    MermaidArcher     = 32,
    MermaidDefender   = 33,
    Swordsmaid        = 34,
    Scuba             = 35,
    Siren             = 36,
    Shark             = 37,
    YellyBelly        = 38,
    Puffer            = 39,
    TridentionAq      = 40,
    CrabAq            = 41,

    // ∑∫ỹriȱŋ Tribe Units
    Polytaur   = 50,
    DragonEgg  = 51,
    BabyDragon = 52,
    FireDragon = 53,

    // Polaris Tribe Units
    IceArcher  = 60,
    BattleSled = 61,
    Mooni      = 62,
    IceFortress= 63,
    Gaami      = 64,

    // Cymanti Tribe Units
    Hexapod    = 70,
    Kiton      = 71,
    Phychi     = 72,
    Shaman     = 73,
    Raychi     = 74,
    Exida      = 75,
    Doomux     = 76,
    MothC      = 77,
    LarvaC     = 78,
    InsectEgg  = 79,
    Boomchi    = 80,
    LivingIsland = 81,

    GiantSuper = 90,
};


class Unit {
public:
    Unit() = default;
    ~Unit() = default;

    // ---- Identity / ownership ----
    UnitId getId() const;
    void setId(UnitId v);

    PlayerId getOwnerId() const;
    void setOwnerId(PlayerId v);

    UnitType getType() const;
    void setType(UnitType v);

    // ---- Position ----
    Pos getPos() const;
    void setPos(Pos p);

    // ---- Stats ----
    int getHealth() const;
    int getMaxHealth() const;
    void setHealth(int v);
    void setMaxHealth(int v);

    float getAttack() const;
    float getDefense() const;
    void setAttack(float v);
    void setDefense(float v);

    int getMovePoints() const;
    void setMovePoints(int v);

    int getRange() const;
    void setRange(int v);

    int getCost() const;
    void setCost(int v);

    int getVisionRange() const;
    void setVisionRange(int v);

    // ---- Spawn requirements (definition-level; prefer UnitDB, but kept here for now) ----
    TechId getRequiredTechToSpawn() const;
    void setRequiredTechToSpawn(TechId v);

    // ---- Turn state ----
    bool movedThisTurn() const;
    bool attackedThisTurn() const;
    void setMovedThisTurn(bool v);
    void setAttackedThisTurn(bool v);

    // ---- Direction tracking (for forced spawns / push logic) ----
    Pos getLastMoveDir() const;
    void setLastMoveDir(Pos d);

    Pos getLastAttackDir() const;
    void setLastAttackDir(Pos d);

    // ---- Status ----
    bool isVeteran() const;
    void setVeteran(bool v);

    bool poisoned() const;
    void setPoisoned(bool v);

    int getKillCounter() const;
    void setKillCounter(int v){killCounter=v;};
    void addKill();

    bool getIsPoisoned() const{return isPoisoned;};

    // ---- Skills (bitmask) ----
    bool hasSkill(UnitSkill skill) const;
    void addSkill(UnitSkill skill);
    void removeSkill(UnitSkill skill);

    // ---- Embark / disembark (port -> raft/juggernaut and back) ----
    // When unit is converted to a naval carrier, we store the original land type here.
    bool isEmbarked() const;
    UnitType getEmbarkedBaseType() const;
    void setEmbarkedBaseType(UnitType v);
    void clearEmbarkedBaseType();

private:
    UnitId id = kNoUnit;
    PlayerId ownerId = kNoPlayer;
    UnitType type = UnitType::Unknown;

    // If the unit is currently embarked (e.g. Raft/Juggernaut), this stores the original land unit type.
    // UnitType::Unknown means "not embarked".
    UnitType embarkedBaseType = UnitType::Unknown;

    Pos pos{};

    // Core stats
    int health = 0;
    int maxHealth = 0;
    float attack = 0;
    float defense = 0;

    TechId requiredTechToSpawn = TechId::Count;

    int movePoints = 0;   // remaining movement points this turn
    int range = 1;
    int cost = 0;
    int visionRange = 0;

    // Turn and status

    int killCounter = 0;
    bool veteran = false;
    bool isPoisoned = false;

    bool hasMovedThisTurn = false;
    bool hasAttackedThisTurn = false;

    Pos lastMoveDir{0,0};
    Pos lastAttackDir{0,0};

    uint64_t skillMask = 0;
};

#endif // GAME_ENGINE_UNIT_H