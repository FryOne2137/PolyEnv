//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#ifndef GAME_ENGINE_TRIBE_H
#define GAME_ENGINE_TRIBE_H

#include <string>
#include <cstdint>

#include "units/Unit.h"
#include "tech/TechDB.h"

enum class TribeType : uint8_t {
    Unknown  = 0,

    // Regular tribes
    XinXi    = 1,
    Imperius = 2,
    Bardur   = 3,
    Oumaji   = 4,
    Kickoo   = 5,
    Hoodrick = 6,
    Luxidoor = 7,
    Vengir   = 8,
    Zebasi   = 9,
    AiMo     = 10,
    Quetzali = 11,
    Yadakk   = 12,

    // Special tribes
    Aquarion = 13,
    Elyrion  = 14,   // ∑∫ỹriȱŋ
    Polaris  = 15,
    Cymanti  = 16,
};

class Tribe {
public:
    Tribe(TribeType type, std::string name, uint8_t stars, uint16_t startScore, TechId tech, UnitType startUnitType);

    TribeType getType() const;
    uint8_t getStartStars() const;
    uint16_t getStartScore() const;
    const std::string& getName() const;
    TechId getStartTech() const;

    const Unit& getStartUnit() const;

    // --- Defaults from wiki (helpers) ---
    static const char* defaultName(TribeType type);
    static uint8_t defaultStartStars(TribeType type);
    static uint16_t defaultStartScore(TribeType type);

    static TechId defaultStartTech(TribeType type);
    static UnitType defaultStartUnitType(TribeType type);
    static Tribe makeDefault(TribeType type);

private:
    TribeType tribeType = TribeType::Unknown;
    std::string tribeName;

    Unit startUnit{};
    uint8_t startStars = 0;
    uint16_t startScore = 0;

    TechId startTech = TechId::Count;
};

#endif // GAME_ENGINE_TRIBE_H