//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Player.h"

#include <algorithm>

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

void Player::addTech(TechId tech) {
    if (tech == TechId::Count) return;

    const uint8_t id = static_cast<uint8_t>(tech);
    if (id >= 64) return; // mask supports up to 64 techs

    const uint64_t bit = (1ull << id);
    if ((techMask & bit) != 0ull) return; // already owned

    techMask |= bit;
    techs.push_back(tech);
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

void Player::removeUnit(UnitId id) {
    if (id == kNoUnit) return;
    auto it = std::remove(units.begin(), units.end(), id);
    if (it != units.end()) {
        units.erase(it, units.end());
    }
}
