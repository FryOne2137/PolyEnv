// ScoreSystem.cpp
//
// Created by Fryderyk Niedzwiecki on 04/02/2026.
//

#include "ScoreSystem.h"

#include <algorithm>

#include "CitySystem.h"
#include "Game.h"
#include "PlayerSystem.h"
#include "UnitSystem.h"
#include "VisionSystem.h"

int ScoreSystem::getScore(const Game& game, const PlayerId pid) {

    const Map& map = game.getMap();


    int score = 0;

    // ---- base (tribe) ----
    score += startingScore(PlayerSystem::getTribeType(game,pid));

    // ---- MAP / EXPLORATION ----
    const int revealedTiles = VisionSystem::countRevealedTiles(game, pid);
    score += exploredTiles(revealedTiles); // ✅ 5 pkt za każdy odsłonięty tile

    // ---- CITIES ----
    // ---- CITIES ----
    for (CityId cid : PlayerSystem::getCities(game,pid)) {

        CitySystem::cityExists(game,cid);

        score += cityLevel(CitySystem::getCityLevel(game,cid), CitySystem::getCityPopulation(game,cid));

        int territoryCount = 0;
        int monumentsCount = 0;

        for (const Tile& t : map.allTiles()) {
            if (t.getTerritoryCityId() != cid)
                continue;

            // territory score
            ++territoryCount;

            // monument score (400 per monument)
            if (isMonument(t.getBuildingType())) {
                ++monumentsCount;
            }
        }

        score += territoryTiles(territoryCount);
        score += monuments(monumentsCount); // 400 * count
    }

    // ---- UNITS ----
    int totalUnitCostStars = 0;
    int superCount = 0;

    // for (UnitId uid : player.getUnits()) {
    for (UnitId uid : PlayerSystem::getUnits(game,pid)) {
        if (!UnitSystem::unitExists(game, uid))
            continue;

        const int cost = UnitSystem::isEmbarked(game, uid)
            ? getBaseUnitCostStars(game, uid)
            : UnitSystem::getCost(game, uid);

        totalUnitCostStars += std::max(0, cost);

        if (isSuperUnit(UnitSystem::getType(game, uid))) {
            ++superCount;
        }
    }

    score += unitCostStars(totalUnitCostStars);
    score += superUnits(superCount);



    // ---- TECHNOLOGIES ----
    for (TechId tech : PlayerSystem::getTechs(game,pid)) {
        const TechTier tier = TechDB::getTech(tech).tier;
        score += techTier(static_cast<int>(tier)); // tier 1->100, tier2->200, tier3->300

    }

    return score;
}


// ===== PRIVATE RULES (1:1 wiki) =====

int ScoreSystem::startingScore(TribeType tribe) {
    // switch (tribe) {
        // case TribeType::Aquarion: return 415;
        //
        // case TribeType::XinXi:
        // case TribeType::Imperius:
        // case TribeType::Bardur:
        // case TribeType::Kickoo:
        // case TribeType::Luxidoor:
        // case TribeType::Elyrion:
        //     return 515;
        //
        // case TribeType::Oumaji:   return 520;
        //
        // case TribeType::Zebasi:
        // case TribeType::Yadakk:
        //     return 615;
        //
        // case TribeType::Quetzali:
        // case TribeType::Hoodrick:
        //     return 620;
        //
        // case TribeType::Polaris:
        // case TribeType::Cymanti:
        //     return 630;
        //
        // case TribeType::Vengir:
        // case TribeType::AiMo:
        //     return 730;

        // default:
            return 0;
    // }
}

int ScoreSystem::exploredTiles(int tiles) {
    return std::max(0, tiles) * 5;
}

int ScoreSystem::territoryTiles(int tiles) {
    return std::max(0, tiles) * 20;
}

int ScoreSystem::unitCostStars(int stars) {
    return std::max(0, stars) * 5;
}

int ScoreSystem::superUnits(int count) {
    return std::max(0, count) * 50;
}

int ScoreSystem::cityLevel(int level, int population) {
    const int lvl = std::max(1, level);
    const int pop = std::max(0, population);

    int score = 100;

    // Level-up contributions (bez żadnej populacji bieżącej!)
    for (int k = 2; k <= lvl; ++k) {
        score += (k * 5) + (50 - 5 * k);
    }

    // ✅ Bieżąca populacja (progress) liczona natychmiast, tylko raz
    score += pop * 5;

    return score;
}


int ScoreSystem::parks(int count) {
    return std::max(0, count) * 250;
}

int ScoreSystem::monuments(int count) {
    return std::max(0, count) * 400;
}

int ScoreSystem::templeLevel(int level) {
    const int lvl = std::clamp(level, 1, 5);
    return 100 + (lvl - 1) * 100;
}

int ScoreSystem::techTier(int tier) {
    return std::max(0, tier) * 100;
}

// ---- local helpers (ScoreSystem.cpp only) ----
bool ScoreSystem::isSuperUnit(UnitType t) {
    // Keep this conservative: count only true super units.
    // If you later tag them in UnitDB, replace this function.
    switch (t) {
        case UnitType::Giant:
        case UnitType::Gaami:
        case UnitType::CrabAq:
        case UnitType::LivingIsland:
        case UnitType::GiantSuper:
        case UnitType::Juggernaut: // if you treat Juggernaut as super; if not, remove it
            return true;
        default:
            return false;
    }
}

int ScoreSystem::costStarsByUnitType(UnitType t) {
    // Minimal fallback costs for base types.
    // Replace with UnitDB lookup when you have it.
    switch (t) {
        case UnitType::Warrior:    return 2;
        case UnitType::Archer:     return 3;
        case UnitType::Defender:   return 3;
        case UnitType::Rider:      return 3;
        case UnitType::MindBender: return 5;
        case UnitType::Swordsman:  return 5;
        case UnitType::Catapult:   return 8;
        case UnitType::Cloak:      return 8;
        case UnitType::Knight:     return 8;

            // Navals (if you ever need them as base)
        case UnitType::Raft:       return 2;

            // Super-ish / special (often not bought with stars, but keep safe)
        case UnitType::Giant:
        case UnitType::Gaami:
        case UnitType::CrabAq:
        case UnitType::LivingIsland:
        case UnitType::GiantSuper:
            return 0;

        default:
            // fallback: use stored cost if you don't know
            return -1;
    }
}

int ScoreSystem::getBaseUnitCostStars(const Game& game, UnitId uid) {
    // If embarked, score should use the base land unit cost.
    const UnitType base = UnitSystem::getEmbarkedBaseType(game, uid);
    const int mapped = costStarsByUnitType(base);
    if (mapped >= 0) return mapped;

    // fallback: if mapping missing, use current stored cost
    return UnitSystem::getCost(game, uid);
}

bool ScoreSystem::isMonument(BuildingTypeEnum b) {
    switch (b) {
        case BuildingTypeEnum::AltarOfPeace:
        case BuildingTypeEnum::EmperorsTomb:
        case BuildingTypeEnum::EyeOfGod:
        case BuildingTypeEnum::GateOfPower:
        case BuildingTypeEnum::GrandBazaar:
        case BuildingTypeEnum::ParkOfFortune:
        case BuildingTypeEnum::TowerOfWisdom:
            return true;
        default:
            return false;
    }
}