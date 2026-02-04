//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_PLAYER_H
#define GAME_ENGINE_PLAYER_H

#include <cstdint>
#include <vector>
#include <array>
#include "tribes/Tribe.h"
#include "tech/TechDB.h"   // <- musi dostarczać TechId (enum class TechId ...)
#include "units/Unit.h"
#include "terrain/BuildingTypeEnum.h"

#include "Core/Ids.h"

class Player {
public:
    Player() = default;
    Player(PlayerId id, TribeType tribe, int startStars, CityId capital);

    PlayerId getId() const;
    TribeType getTribeType() const;

    int getStars() const;
    bool spendStars(int amount);   // returns false if not enough
    void addStars(int amount);
    uint16_t getKillerCount() const;
    bool addKill(int kills=1);

    // --- Pacifist (Altar of Peace) tracking ---
    uint8_t getNoAttackTurns() const;
    void setAttackedThisTurn(bool v);
    bool getAttackedThisTurn() const;
    void resetNoAttackTurns();
    void incrementNoAttackTurns();

    bool hasTech(TechId tech) const;
    void addTech(TechId tech);
    const std::vector<TechId>& getTechs() const;
    bool hasAllTechUnlocked() const;

    CityId getCapitalId() const;
    void setCapitalId(CityId id);

    const std::vector<CityId>& getCities() const;
    const std::vector<UnitId>& getUnits() const;

    void addCity(CityId id);
    void addUnit(UnitId id);
    void removeUnit(UnitId id);

    void removeCity(CityId id);
    // --- Monuments list helpers ---
    std::vector<BuildingTypeEnum> getEarnedMonuments() const;
    std::vector<BuildingTypeEnum> getPlacedMonuments() const;
    std::vector<BuildingTypeEnum> getOwnedMonuments() const; // union(earned, placed)

    // --- Monuments ---
    bool hasEarnedMonument(BuildingTypeEnum monument) const;
    bool hasPlacedMonument(BuildingTypeEnum monument) const;

    bool addMonument(BuildingTypeEnum monument);   // called when condition is met
    bool placeMonument(BuildingTypeEnum monument);   // called when building is placed
    static uint64_t monumentBit(BuildingTypeEnum monument);

private:

    PlayerId playerId = 0;
    TribeType tribeType = TribeType::Unknown;

    uint16_t killerCount = 0;
    uint8_t noAttackTurns = 0;
    bool attackedThisTurn = false;

    int stars = 0;
    int score=0;

    CityId capitalId = kNoCity;

    uint64_t techMask = 0;
    std::vector<TechId> techs;
    std::vector<UnitId> units;
    std::vector<CityId> cities;

    // Monuments state (unique per player)
    uint64_t earnedMonumentMask = 0;   // condition completed
    uint64_t placedMonumentMask = 0;   // structure already built

    static constexpr std::array<BuildingTypeEnum, 7> kAllMonuments = {
        BuildingTypeEnum::AltarOfPeace,
        BuildingTypeEnum::EmperorsTomb,
        BuildingTypeEnum::EyeOfGod,
        BuildingTypeEnum::GateOfPower,
        BuildingTypeEnum::GrandBazaar,
        BuildingTypeEnum::ParkOfFortune,
        BuildingTypeEnum::TowerOfWisdom
    };
};



#endif //GAME_ENGINE_PLAYER_H