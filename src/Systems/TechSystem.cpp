//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//

#include "TechSystem.h"

#include <algorithm>
#include <vector>

#include "tech/TechDB.h"

bool TechSystem::canBuyTech(const Player& pl, TechId tech) {
    if (pl.hasTech(tech))
        return false;

    TechId prereq = TechDB::getPrerequisite(tech);
    if (prereq != TechId::Count && !pl.hasTech(prereq))
        return false;

    return true;
}

bool TechSystem::buyTech(Player& pl, TechId tech) {
    if (!canBuyTech(pl, tech))
        return false;

    const int cities = std::max(1, (int)pl.getCities().size());
    const bool literacy = pl.hasTech(TechId::Philosophy);
    const int price = TechDB::calculatePrice(tech, cities, literacy);

    if (!pl.spendStars(price))
        return false;

    pl.addTech(tech);
    return true;
}

bool TechSystem::hasTech(const Player& pl, TechId tech) {
    return pl.hasTech(tech);
}

const std::vector<TechId>& TechSystem::getTechs(const Player& pl) {
    static thread_local std::vector<TechId> cache;
    cache.clear();
    cache.reserve(static_cast<size_t>(TechId::Count));

    for (uint8_t i = 0; i < static_cast<uint8_t>(TechId::Count); ++i) {
        const TechId t = static_cast<TechId>(i);
        if (pl.hasTech(t)) {
            cache.push_back(t);
        }
    }

    return cache;
}