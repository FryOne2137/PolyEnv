//
// Created by Fryderyk Niedzwiecki on 06/02/2026.
//

#ifndef GAME_ENGINE_CITIESSYSTEM_H
#define GAME_ENGINE_CITIESSYSTEM_H

#include "Game.h"
#include "City.h"
#include "Map.h"
#include <string>



class CitySystem {
public:

    static bool cityExists(const Game& game, CityId cityId);


    static uint8_t getCityLevel(const Game&, CityId);
    static int16_t getCityPopulation(const Game&, CityId);
    static uint8_t getCityOwner(const Game&, CityId);
    static uint8_t getCityStarsPerRound(const Game&, CityId);
    static uint8_t getCityUnitsCount(const Game&, CityId);
    static uint8_t getCityMaxUnitCapacity(const Game&, CityId);

    static Pos getCityPos(const Game& game, CityId cityId);

    static const std::string getCityName(const Game &game, CityId cityId);

    static CityId getCityId(const Game &game, CityId cityId);
    static CityId pickCityForConvertedUnit(const Game& game, PlayerId owner);

    static bool setCityPos(Game &game, CityId cityId, Pos pos);

    static bool setCityId(Game &game, CityId cityId, CityId newId);

    static bool addUnitToCity(Game &game, UnitId unitId, CityId cityId, bool checkCapacity=true) ;
    static bool removeUnitFromCity(Game& game, UnitId unitId, CityId cityId) ;
    static bool transferUnitBetweenCities(Game& game, UnitId unitId, CityId fromCityId, CityId toCityId) ;

    static bool setCityOwner(Game& game, CityId cityId, uint8_t ownerId);
    static bool setCityName(Game& game, CityId cityId, const std::string& name);
    static bool setCityCapital(Game& game, CityId cityId, bool isCapital);

    static bool setCityLevel(Game& game, CityId cityId, uint8_t level);
    static bool setCityPopulation(Game& game, CityId cityId, int16_t population);
    static bool setCityStarsPerRound(Game& game, CityId cityId, uint8_t starsPerRound);

    static bool addPopulation(Game& game, CityId cityId, uint16_t amount);

    // ---- High-level city actions ----
    static bool foundCityFromVillage(Game& game, PlayerId owner, Pos pos);
    static bool captureCityAt(Game& game, PlayerId newOwner, Pos pos);

    // ---- Helpers ----
    static void reassignUnitToCity(Game &game, UnitId uid, CityId newCityId, bool checkCapacity=true);
    static void claimFreeTerritoryRadius1(Game& game, CityId cid, SettlementId citySid, Pos center);

    static bool initCapital(Game &game, PlayerId owner, CityId cid, Pos capPos);
};


#endif //GAME_ENGINE_CITIESSYSTEM_H