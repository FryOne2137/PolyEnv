//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_PLAYER_H
#define GAME_ENGINE_PLAYER_H

#include <cstdint>
#include <vector>

#include "tribes/Tribe.h"
#include "tech/TechDB.h"   // <- musi dostarczać TechId (enum class TechId ...)
#include "units/Unit.h"

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

    bool hasTech(TechId tech) const;
    void addTech(TechId tech);
    const std::vector<TechId>& getTechs() const;

    CityId getCapitalId() const;
    void setCapitalId(CityId id);

    const std::vector<CityId>& getCities() const;
    const std::vector<UnitId>& getUnits() const;

    void addCity(CityId id);
    void addUnit(UnitId id);
    void removeUnit(UnitId id);

    void removeCity(CityId id);

private:
    PlayerId playerId = 0;
    TribeType tribeType = TribeType::Unknown;

    int stars = 0;

    CityId capitalId = kNoCity;

    uint64_t techMask = 0;
    std::vector<TechId> techs;
    std::vector<UnitId> units;
    std::vector<CityId> cities;
};

#endif //GAME_ENGINE_PLAYER_H