//
// Created by Fryderyk Niedzwiecki on 07/02/2026.
//

#include "CityUpgradeSystem.h"

#include "CitySystem.h"
#include "PlayerSystem.h"

// Jeśli masz osobne systemy, podepnij tu:
// #include "UnitSystem.h"
// #include "TerritorySystem.h"

#include <cstdint>

namespace {

// Wiki: awans na poziom n wymaga n populacji (od poprzedniego poziomu).
// To ma sens, jeśli w City trzymasz "progress/population toward next level" (nie total).
static inline uint8_t requiredPopForNextLevel(uint8_t nextLevel) {
    return nextLevel; // lvl2 -> 2, lvl3 -> 3, lvl4 -> 4, ...
}

static inline bool isValidChoiceForLevel(CityUpgradeChoice c, uint8_t newLevel) {
    if (newLevel == 2) return c == CityUpgradeChoice::Workshop || c == CityUpgradeChoice::Explorer;
    if (newLevel == 3) return c == CityUpgradeChoice::CityWall || c == CityUpgradeChoice::Resources;
    if (newLevel == 4) return c == CityUpgradeChoice::PopulationGrowth || c == CityUpgradeChoice::BorderGrowth;
    if (newLevel >= 5) return c == CityUpgradeChoice::Park || c == CityUpgradeChoice::SuperUnit;
    return false;
}

} // namespace

bool CityUpgradeSystem::canUpgrade(const Game& game, CityId cityId) {
    if (!CitySystem::cityExists(game, cityId)) return false;

    const uint8_t level = CitySystem::getCityLevel(game, cityId);
    const uint8_t nextLevel = static_cast<uint8_t>(level + 1);

    const int16_t pop = CitySystem::getCityPopulation(game, cityId);
    if (pop < 0) return false;

    const uint8_t need = requiredPopForNextLevel(nextLevel);
    return pop >= need;
}

uint8_t CityUpgradeSystem::getNextLevel(const Game& game, CityId cityId) {
    if (!CitySystem::cityExists(game, cityId)) return 0;
    return static_cast<uint8_t>(CitySystem::getCityLevel(game, cityId) + 1);
}

CityUpgradeOptions CityUpgradeSystem::getUpgradeOptions(const Game& game, CityId cityId) {
    CityUpgradeOptions out{};

    if (!CitySystem::cityExists(game, cityId)) return out;

    const uint8_t newLevel = static_cast<uint8_t>(CitySystem::getCityLevel(game, cityId) + 1);

    // Zwracaj zawsze "dwie opcje" dla danego levelu.
    if (newLevel == 2) { out.a = CityUpgradeChoice::Workshop; out.b = CityUpgradeChoice::Explorer; return out; }
    if (newLevel == 3) { out.a = CityUpgradeChoice::CityWall; out.b = CityUpgradeChoice::Resources; return out; }
    if (newLevel == 4) { out.a = CityUpgradeChoice::PopulationGrowth; out.b = CityUpgradeChoice::BorderGrowth; return out; }
    if (newLevel >= 5) { out.a = CityUpgradeChoice::Park; out.b = CityUpgradeChoice::SuperUnit; return out; }

    return out;
}

bool CityUpgradeSystem::tryUpgrade(Game& game, CityId cityId, CityUpgradeChoice choice) {
    if (!CitySystem::cityExists(game, cityId)) return false;

    const uint8_t oldLevel = CitySystem::getCityLevel(game, cityId);
    const uint8_t newLevel = static_cast<uint8_t>(oldLevel + 1);

    if (!isValidChoiceForLevel(choice, newLevel)) return false;
    if (!canUpgrade(game, cityId)) return false;

    // 1) "Zapłać" populacją za upgrade (incremental).
    const uint8_t need = requiredPopForNextLevel(newLevel);
    const int16_t pop = CitySystem::getCityPopulation(game, cityId);
    CitySystem::setCityPopulation(game, cityId, static_cast<int16_t>(pop - need));

    // 2) Podbij level.
    CitySystem::setCityLevel(game, cityId, newLevel);

    // 3) Bazowy efekt upgrade: +1 income / turn (wg wiki).
    // (jeśli u Ciebie income jest liczony dynamicznie, to zamiast set/get trzymaj to w kalkulacji)
    const uint8_t oldIncome = CitySystem::getCityStarsPerRound(game, cityId);
    CitySystem::setCityStarsPerRound(game, cityId, static_cast<uint8_t>(oldIncome + 1));

    // 4) Nagroda zależna od levelu i wyboru.
    const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(game, cityId));

    switch (choice) {
        case CityUpgradeChoice::Workshop: {
            // Workshop: +1 star/turn w tym mieście.
            CitySystem::setCityWorkshopEnabled(game, cityId, true);
            // Jeśli workshop nie jest liczony automatycznie gdzie indziej:
            CitySystem::setCityStarsPerRound(game, cityId, static_cast<uint8_t>(CitySystem::getCityStarsPerRound(game, cityId) + 1));
        } break;

        case CityUpgradeChoice::Explorer: {
            // Explorer: u Ciebie to powinno odpalić mechanikę "zwiadowca/odkrywanie".
            // Najczyściej: UnitSystem::spawnExplorer(game, owner, CitySystem::getCityPos(game, cityId));
            // albo Game::triggerExplorerFromCity(cityId).
            // TODO: podepnij do Twojego UnitSystem/VisibilitySystem.
            return false; // dopóki nie podepniesz hooka, lepiej fail niż "puste kliknięcie"
        } break;

        case CityUpgradeChoice::CityWall: {
            CitySystem::setCityWallEnabled(game, cityId, true);
        } break;

        case CityUpgradeChoice::Resources: {
            // Resources: natychmiast +5 stars dla gracza.
            PlayerSystem::addStars(game, owner, 5);
        } break;

        case CityUpgradeChoice::PopulationGrowth: {
            // +3 population natychmiast.
            CitySystem::addPopulation(game, cityId, 3);
        } break;

        case CityUpgradeChoice::BorderGrowth: {
            // Border Growth: rozszerz granice miasta.
            // Najlepiej: dedykowany TerritorySystem, np. expandCityBorder(game, cityId).
            // Jeśli masz tylko claimFreeTerritoryRadius1, to NIE emuluj na siłę – zrób hook.
            // TODO: TerritorySystem::expandBorder(game, cityId);
            return false;
        } break;

        case CityUpgradeChoice::Park: {
            // Park: +1 star/turn i punkty. Punkty jeśli liczysz osobno → ScoreSystem.
            CitySystem::addCityParkCount(game, cityId, 1);
            // Jeśli park nie jest liczony automatycznie gdzie indziej:
            CitySystem::setCityStarsPerRound(game, cityId, static_cast<uint8_t>(CitySystem::getCityStarsPerRound(game, cityId) + 1));
        } break;

        case CityUpgradeChoice::SuperUnit: {
            // Super Unit: spawn "gianta"/super jednostki.
            // TODO: UnitSystem::spawnSuperUnitForCity(game, owner, cityId);
            return false;
        } break;
    }

    return true;
}