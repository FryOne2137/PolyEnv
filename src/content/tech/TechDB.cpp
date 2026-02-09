//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//

#include "TechDB.h"
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>

static constexpr TechId NONE = TechId::Count;

static constexpr std::array<TechData, TechDB::TECH_COUNT> TECHS = {{
    // Tier 1
    {TechId::Climbing,     "Climbing",     TechTier::Tier1, NONE},
    {TechId::Fishing,      "Fishing",      TechTier::Tier1, NONE},
    {TechId::Hunting,      "Hunting",      TechTier::Tier1, NONE},
    {TechId::Organization, "Organization", TechTier::Tier1, NONE},
    {TechId::Riding,       "Riding",       TechTier::Tier1, NONE},

    // Tier 2
    {TechId::Archery,      "Archery",      TechTier::Tier2, TechId::Hunting},
    {TechId::Ramming,      "Ramming",      TechTier::Tier2, TechId::Fishing},
    {TechId::Farming,      "Farming",      TechTier::Tier2, TechId::Organization},
    {TechId::Forestry,     "Forestry",     TechTier::Tier2, TechId::Hunting},
    {TechId::FreeSpirit,   "Free Spirit",  TechTier::Tier2, TechId::Riding},
    {TechId::Meditation,   "Meditation",   TechTier::Tier2, TechId::Climbing},
    {TechId::Mining,       "Mining",       TechTier::Tier2, TechId::Climbing},
    {TechId::Roads,        "Roads",        TechTier::Tier2, TechId::Riding},
    {TechId::Sailing,      "Sailing",      TechTier::Tier2, TechId::Fishing},
    {TechId::Strategy,     "Strategy",     TechTier::Tier2, TechId::Organization},

    // Tier 3
    {TechId::Aquatism,     "Aquatism",     TechTier::Tier3, TechId::Ramming},
    {TechId::Chivalry,     "Chivalry",     TechTier::Tier3, TechId::FreeSpirit},
    {TechId::Construction, "Construction", TechTier::Tier3, TechId::Farming},
    {TechId::Diplomacy,    "Diplomacy",    TechTier::Tier3, TechId::Strategy},
    {TechId::Mathematics,  "Mathematics",  TechTier::Tier3, TechId::Forestry},
    {TechId::Navigation,   "Navigation",   TechTier::Tier3, TechId::Sailing},
    {TechId::Philosophy,   "Philosophy",   TechTier::Tier3, TechId::Meditation},
    {TechId::Smithery,     "Smithery",     TechTier::Tier3, TechId::Mining},
    {TechId::Spiritualism, "Spiritualism", TechTier::Tier3, TechId::Archery},
    {TechId::Trade,         "Trade",       TechTier::Tier3, TechId::Roads},
}};

const TechData& TechDB::getTech(TechId id) {
    return TECHS[static_cast<uint8_t>(id)];
}

TechId TechDB::getPrerequisite(TechId id) {
    return getTech(id).prerequisite;
}

static bool containsTech(const std::vector<TechId>& v, TechId id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

std::vector<TechId> TechDB::getObtainableTechsThisRound(const std::vector<TechId>& ownedTechs) {
    std::vector<TechId> result;
    result.reserve(TechDB::TECH_COUNT);

    for (size_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        TechId id = static_cast<TechId>(i);
        const TechData& t = getTech(id);

        // skip already owned
        if (containsTech(ownedTechs, id))
            continue;

        // Obtainable this round:
        // - any Tier1 tech
        // - OR any tech whose prerequisite (lower tier) is already owned
        if (t.tier == TechTier::Tier1 || (t.prerequisite != NONE && containsTech(ownedTechs, t.prerequisite))) {
            result.push_back(id);
        }
    }

    return result;
}

TechId TechDB::rollRandomObtainableTechThisRound(
    const std::vector<TechId>& ownedTechs,
    uint32_t r
) {
    std::vector<TechId> pool = getObtainableTechsThisRound(ownedTechs);
    if (pool.empty()) return TechId::Count;
    return pool[r % pool.size()];
}


// Wiki formula:
// cost = tier * cities + 4
// Literacy: -33% (ceil)
int TechDB::calculatePrice(TechId id, int cityCount, bool hasLiteracy) {
    const TechData& t = getTech(id);

    int tier = static_cast<int>(t.tier);
    int baseCost = tier * cityCount + 4;

    if (!hasLiteracy)
        return baseCost;

    // Literacy discount = 33%, rounded UP
    double discounted = baseCost * (2.0 / 3.0);
    return static_cast<int>(std::ceil(discounted));
}