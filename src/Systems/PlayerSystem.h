//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#ifndef GAME_ENGINE_PLAYERSYSTEM_H
#define GAME_ENGINE_PLAYERSYSTEM_H

#include "Core/Ids.h"
#include "tribes/Tribe.h"
#include "tech/TechDB.h"
#include "terrain/BuildingTypeEnum.h"

#include <vector>
#include <cstdint>

class Game;

// PlayerSystem is the ONLY allowed way for other systems to access or modify Player state.
// Never use game.getPlayer(...)-> directly outside this system.
class PlayerSystem {
public:

    // ---- Existence ----
    static bool playerExists(const Game& game, PlayerId pid);

    // ---- Identity / tribe ----
    static PlayerId getId(const Game& game, PlayerId pid);
    static TribeType getTribeType(const Game& game, PlayerId pid);

    // ---- Stars ----
    static int getStars(const Game& game, PlayerId pid);
    static bool spendStars(Game& game, PlayerId pid, int amount);
    static void addStars(Game& game, PlayerId pid, int amount);

    // ---- Kills ----
    static uint16_t getKillerCount(const Game& game, PlayerId pid);
    static bool addKill(Game& game, PlayerId pid, int kills = 1);

    // ---- Pacifist (Altar of Peace) ----
    static uint8_t getNoAttackTurns(const Game& game, PlayerId pid);
    static void setAttackedThisTurn(Game& game, PlayerId pid, bool v);
    static bool getAttackedThisTurn(const Game& game, PlayerId pid);
    static void resetNoAttackTurns(Game& game, PlayerId pid);
    static void incrementNoAttackTurns(Game& game, PlayerId pid);

    // ---- Tech ----
    static bool hasTech(const Game& game, PlayerId pid, TechId tech);
    static void addTech(Game& game, PlayerId pid, TechId tech);
    static const std::vector<TechId>& getTechs(const Game& game, PlayerId pid);
    static bool hasAllTechUnlocked(const Game& game, PlayerId pid);

    // ---- Capital ----
    static CityId getCapitalId(const Game& game, PlayerId pid);
    static void setCapitalId(Game& game, PlayerId pid, CityId id);

    // ---- Cities / Units lists ----
    static const std::vector<CityId>& getCities(const Game& game, PlayerId pid);
    static const std::vector<UnitId>& getUnits(const Game& game, PlayerId pid);

    static void addCity(Game& game, PlayerId pid, CityId id);
    static void removeCity(Game& game, PlayerId pid, CityId id);

    static void addUnit(Game& game, PlayerId pid, UnitId id);
    static void removeUnit(Game& game, PlayerId pid, UnitId id);

    // ---- Monuments ----
    static std::vector<BuildingTypeEnum> getEarnedMonuments(const Game& game, PlayerId pid);
    static std::vector<BuildingTypeEnum> getPlacedMonuments(const Game& game, PlayerId pid);
    static std::vector<BuildingTypeEnum> getOwnedMonuments(const Game& game, PlayerId pid);

    static bool hasEarnedMonument(const Game& game, PlayerId pid, BuildingTypeEnum monument);
    static bool hasPlacedMonument(const Game& game, PlayerId pid, BuildingTypeEnum monument);

    static bool addMonument(Game& game, PlayerId pid, BuildingTypeEnum monument);
    static bool placeMonument(Game& game, PlayerId pid, BuildingTypeEnum monument);

    // ---- Diplomacy ----
    static bool hasMet(const Game& game, PlayerId pid, PlayerId other);
    static bool markMet(Game& game, PlayerId pid, PlayerId other);
};

#endif //GAME_ENGINE_PLAYERSYSTEM_H