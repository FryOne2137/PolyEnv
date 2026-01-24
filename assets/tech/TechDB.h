//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//

#ifndef GAME_ENGINE_TECHDB_H
#define GAME_ENGINE_TECHDB_H


#pragma once
#include <cstdint>
#include <string_view>
#include <array>

enum class TechTier : uint8_t {
    Tier1 = 1,
    Tier2 = 2,
    Tier3 = 3
};

// STABILNE ID – NIE ZMIENIAĆ KOLEJNOŚCI
enum class TechId : uint8_t {
    // Tier 1
    Climbing = 0,
    Fishing = 1,
    Hunting = 2,
    Organization = 3,
    Riding = 4,

    // Tier 2
    Archery = 5,
    Ramming = 6,
    Farming = 7,
    Forestry = 8,
    FreeSpirit = 9,
    Meditation = 10,
    Mining = 11,
    Roads = 12,
    Sailing = 13,
    Strategy = 14,

    // Tier 3
    Aquatism = 15,
    Chivalry = 16,
    Construction = 17,
    Diplomacy = 18,
    Mathematics = 19,
    Navigation = 20,
    Philosophy = 21,
    Smithery = 22,
    Spiritualism = 23,
    Trade = 24,

    Count
};

struct TechData {
    TechId id;
    std::string_view name;
    TechTier tier;
    TechId prerequisite; // TechId::Count = brak
};

class TechDB {
public:
    static constexpr uint8_t TECH_COUNT = static_cast<uint8_t>(TechId::Count);

    // Dane techów
    static const TechData& getTech(TechId id);

    // Cena technologii
    static int calculatePrice(TechId id, int cityCount, bool hasLiteracy);

    // Helper
    static TechId getPrerequisite(TechId id);
};


#endif //GAME_ENGINE_TECHDB_H