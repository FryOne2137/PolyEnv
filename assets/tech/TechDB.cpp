//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//

#include "TechDB.h"
#include <cmath>

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