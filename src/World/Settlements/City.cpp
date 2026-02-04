#include "City.h"

// City is currently a data-only type with inline accessors in City.h.
// Keep this translation unit for future non-inline logic (constructors, helpers, etc.).

void City::addUnit(UnitId id) {
    if (id == kNoUnit) return;

    // Do not exceed city capacity.
    if (units.size() >= static_cast<size_t>(maxUnitCapacity())) return;

    // Avoid duplicates.
    if (std::find(units.begin(), units.end(), id) != units.end()) return;

    units.push_back(id);
}

void City::removeUnit(UnitId id) {
    if (id == kNoUnit) return;
    auto it = std::remove(units.begin(), units.end(), id);
    if (it != units.end()) {
        units.erase(it, units.end());
    }
}

uint8_t City::populationNeededToLevelUp() const {
    return getLevel()+1;

}

bool City::levelUp() {
    uint8_t level = getLevel();
    uint8_t population = getPopulation();
    uint8_t populationNeeded = populationNeededToLevelUp();

    if (population < populationNeeded) {
        return false;
    }

    setPopulation(population-populationNeeded);
    setLevel(level+1);
    return true;
}


uint8_t City::maxUnitCapacity() const {
    return getLevel()+1;
}

uint8_t City::getUnitsCount() const {
    return units.size();
}

uint8_t City::getStarsPerRound() const {
    uint8_t starsPerRound = getLevel();
    if (isCapitalCity()) {
        starsPerRound+20;
    }
    if (hasWorkshopEnabled()) {
        starsPerRound++;
    }

    return starsPerRound+20;
}

bool City::addPopulation(uint16_t v) {
    population += v;

    if (population >= populationNeededToLevelUp()) {
        return levelUp();
    }


    return true;
}

