//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Player.h"

#include <algorithm>
#include "terrain/BuildingTypeEnum.h"

// ---- Ctors ----
Player::Player(PlayerId id, TribeType tribe, int startStars, CityId capital)
    : playerId(id), tribeType(tribe), stars(startStars), capitalId(capital) {
    if (capitalId != kNoCity) {
        cities.push_back(capitalId);
    }
}

// ---- Identity / tribe ----
PlayerId Player::getId() const {
    return playerId;
}

TribeType Player::getTribeType() const {
    return tribeType;
}

// ---- Economy ----
int Player::getStars() const {
    return stars;
}



bool Player::spendStars(int amount) {
    if (amount <= 0) return true;
    if (stars < amount) return false;
    stars -= amount;
    return true;
}

void Player::addStars(int amount) {
    if (amount <= 0) return;
    stars += amount;
}

// ---- Tech ----

bool Player::hasTech(TechId tech) const {
    const uint8_t id = static_cast<uint8_t>(tech);
    if (tech == TechId::Count) return false;
    if (id >= 64) return false; // mask supports up to 64 techs
    return (techMask & (1ull << id)) != 0ull;
}

const std::vector<TechId>& Player::getTechs() const {
    return techs;
}

// ---- Monuments ----

uint64_t Player::monumentBit(BuildingTypeEnum monument) {
    return 1ull << static_cast<uint8_t>(monument);
}

bool Player::hasEarnedMonument(BuildingTypeEnum monument) const {
    return (earnedMonumentMask & monumentBit(monument)) != 0ull;
}

bool Player::hasPlacedMonument(BuildingTypeEnum monument) const {
    return (placedMonumentMask & monumentBit(monument)) != 0ull;
}

bool Player::addMonument(BuildingTypeEnum monument) {
    const uint64_t bit = monumentBit(monument);
    if ((earnedMonumentMask & bit) != 0ull) return false;
    earnedMonumentMask |= bit;
    return true;
}

bool Player::placeMonument(BuildingTypeEnum monument) {
    const uint64_t bit = monumentBit(monument);
    if ((earnedMonumentMask & bit) == 0ull) return false;
    if ((placedMonumentMask & bit) != 0ull) return false;
    placedMonumentMask |= bit;
    return true;
}

void Player::addTech(TechId tech) {
    if (tech == TechId::Count) return;

    const uint8_t id = static_cast<uint8_t>(tech);
    if (id >= 64) return; // mask supports up to 64 techs

    const uint64_t bit = (1ull << id);
    if ((techMask & bit) != 0ull) return; // already owned

    techMask |= bit;
    techs.push_back(tech);
    if (hasAllTechUnlocked()) {
        (void)addMonument(BuildingTypeEnum::TowerOfWisdom);
    }
}

bool Player::hasAllTechUnlocked() const {
    // TechId is contiguous: [0..TechId::Count-1]
    const uint8_t count = static_cast<uint8_t>(TechId::Count);
    if (count == 0 || count > 64) return false;

    const uint64_t requiredMask =
        (count == 64) ? 0xFFFF'FFFF'FFFF'FFFFull : ((1ull << count) - 1ull);

    return (techMask & requiredMask) == requiredMask;
}

// ---- Cities / Units (ids, not pointers) ----
CityId Player::getCapitalId() const {
    return capitalId;
}

void Player::setCapitalId(CityId id) {
    capitalId = id;
    if (capitalId != kNoCity) {
        if (std::find(cities.begin(), cities.end(), capitalId) == cities.end()) {
            cities.push_back(capitalId);
        }
    }
}

const std::vector<CityId>& Player::getCities() const {
    return cities;
}

const std::vector<UnitId>& Player::getUnits() const {
    return units;
}

void Player::addCity(CityId id) {
    if (id == kNoCity) return;
    if (std::find(cities.begin(), cities.end(), id) != cities.end()) return;
    cities.push_back(id);
}

void Player::removeCity(CityId id) {
    if (id == kNoCity) return;
    auto it = std::remove(cities.begin(), cities.end(), id);
    if (it != cities.end()) {
        cities.erase(it, cities.end());
    }

    // If we lost our capital, clear it (later you can implement capital transfer rules).
    if (capitalId == id) {
        capitalId = kNoCity;
    }
}

void Player::addUnit(UnitId id) {
    if (id == kNoUnit) return;
    if (std::find(units.begin(), units.end(), id) != units.end()) return;
    units.push_back(id);
}

uint16_t Player::getKillerCount() const {
    return killerCount;
}

uint8_t Player::getNoAttackTurns() const {
    return noAttackTurns;
}

void Player::setAttackedThisTurn(bool v) {
    attackedThisTurn = v;
}

bool Player::getAttackedThisTurn() const {
    return attackedThisTurn;
}

void Player::resetNoAttackTurns() {
    noAttackTurns = 0;
}

void Player::incrementNoAttackTurns() {
    if (noAttackTurns < 255) {
        ++noAttackTurns;
    }
}

bool Player::addKill(int kills) {
    killerCount += kills;
    return true;
}

void Player::removeUnit(UnitId id) {
    if (id == kNoUnit) return;
    auto it = std::remove(units.begin(), units.end(), id);
    if (it != units.end()) {
        units.erase(it, units.end());
    }
}

std::vector<BuildingTypeEnum> Player::getEarnedMonuments() const {
    std::vector<BuildingTypeEnum> out;
    out.reserve(kAllMonuments.size());
    for (BuildingTypeEnum m : kAllMonuments) {
        if (hasEarnedMonument(m)) out.push_back(m);
    }
    return out;
}

std::vector<BuildingTypeEnum> Player::getPlacedMonuments() const {
    std::vector<BuildingTypeEnum> out;
    out.reserve(kAllMonuments.size());
    for (BuildingTypeEnum m : kAllMonuments) {
        if (hasPlacedMonument(m)) out.push_back(m);
    }
    return out;
}

std::vector<BuildingTypeEnum> Player::getOwnedMonuments() const {
    std::vector<BuildingTypeEnum> out;
    out.reserve(kAllMonuments.size());
    for (BuildingTypeEnum m : kAllMonuments) {
        if (hasEarnedMonument(m) || hasPlacedMonument(m)) out.push_back(m);
    }
    return out;
}



