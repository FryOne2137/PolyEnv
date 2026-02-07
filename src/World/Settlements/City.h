#ifndef GAME_ENGINE_CITY_H
#define GAME_ENGINE_CITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "Settlement.h"
#include "Core/Ids.h"


class City : public Settlement {
public:
    CityId getCityId() const { return cityId; }
    uint8_t getLevel() const { return level; }
    int16_t getPopulation() const { return population; }
    uint8_t getStarsPerRound() const;
    uint8_t getOwnerId() const { return ownerId; }
    uint8_t populationNeededToLevelUp() const ;

    bool getIsCapital() const { return isCapital; }
    bool getIsInfiltrated() const { return isInfiltrated; }

    const std::string& getName() const { return name; }
    bool hasCityWallEnabled() const { return hasCityWall; }
    bool hasWorkshopEnabled() const { return hasWorkshop; }
    bool isCapitalCity() const { return isCapital; }
    void setCapital(bool v) { isCapital = v; }

    void setIsInfiltrated(bool v) { isInfiltrated=v; }
    // Buildings / upgrades toggles
    void setCityWallEnabled(bool v) { hasCityWall = v; }
    void setWorkshopEnabled(bool v) { hasWorkshop = v; }



    void setOwnerId(uint8_t v) { ownerId = v; }
    void setName(const std::string& n) { name = n; }

    void setCityId(CityId id) { cityId = id; }
    void setLevel(uint8_t v) { level = v; }
    void setPopulation(int16_t v) { population = v; }
    void setStarsPerRound(uint8_t v) { starsPerRound = v; }

    void addUnit(UnitId id);
    void removeUnit(UnitId id);


    bool addPopulation(uint16_t v);

    uint8_t maxUnitCapacity() const;
    uint8_t getUnitsCount() const;

    uint8_t getParkCount() const{return parkCount;};
    void setParkCount(uint8_t v) { parkCount=v;};
    void addParkCount() { parkCount++;};


private:
    CityId cityId = kNoCity;
    uint8_t ownerId = 0;
    uint8_t level = 1;
    int16_t population = 0;
    uint8_t starsPerRound = 1;
    uint8_t parkCount = 0;

    bool isCapital = false;
    bool isInfiltrated=false;

    bool hasCityWall = false;
    bool hasWorkshop = false;

    std::vector<UnitId> units;


    std::string name;
    bool levelUp();
};

#endif