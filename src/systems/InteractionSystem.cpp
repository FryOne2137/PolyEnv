//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "InteractionSystem.h"

#include "../game/Game.h"
#include "world/Map.h"
#include "world/Tile.h"
#include "systems/CitySystem.h"
#include "systems/MonumentSystem.h"

#include "terrain/ResourcesEnum.h"
#include "terrain/SettlementTypeEnum.h"
#include "systems/VisionSystem.h"
#include "../content/tech/TechDB.h"
#include "terrain/VisibilityEnum.h"
#include "../content/units/UnitFactory.h"
#include "systems/UnitSystem.h"
#include "systems/PlayerSystem.h"

#include <cstdint>
#include <iostream>
#include <ostream>
#include <vector>

#include "UnitSpawnSystem.h"
#include "systems/MovementSystem.h"  // Added include as per instructions



// --- deterministic RNG (seed + position) ---
static inline uint32_t mix32(uint32_t x) {
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    x ^= (x >> 16);
    return x;
}

static inline uint32_t ruinRng(uint32_t worldSeed, Pos pos, uint32_t salt) {
    uint32_t h = worldSeed;
    h ^= mix32(static_cast<uint32_t>(pos.x) * 0x27d4eb2du);
    h ^= mix32(static_cast<uint32_t>(pos.y) * 0x165667b1u);
    h ^= mix32(salt);
    return mix32(h);
}


static constexpr int kStarfishReward = 8;
static constexpr int kRuinStarsReward = 10;


static const char* ruinRewardToString(InteractionSystem::RuinReward r) {
    switch (r) {
        case InteractionSystem::RuinReward::Stars:        return "Stars";
        case InteractionSystem::RuinReward::Tech:         return "Tech";
        case InteractionSystem::RuinReward::Population:   return "Population";
        case InteractionSystem::RuinReward::Explorer:     return "Explorer";
        case InteractionSystem::RuinReward::VeteranUnit:  return "VeteranUnit";
        default:                                         return "<Unknown>";
    }
}

void InteractionSystem::onUnitEnteredTile(Game& game, UnitId unitId, Pos pos) {
    if (!UnitSystem::unitExists(game, unitId)) return;

    // Bezpieczeństwo: Interaction odpalamy po udanym ruchu, ale sprawdźmy bounds
    if (!game.getMap().inBounds(pos)) return;

    // Kolejność nieprzypadkowa:
    // 1) rzeczy “zbieralne” (starfish)
    // 2) rzeczy “statusowe” (ruin)
    // 3) rzeczy zmieniające kontrolę / struktury (village->city)
    handleStarfish(game, unitId, pos);
    handleRuin(game, unitId, pos);
    // handleVillage(game, unitId, pos);
}

void InteractionSystem::handleStarfish(Game& game, UnitId unitId, Pos pos) {
    if (!UnitSystem::unitExists(game, unitId)) return;

    Tile& tile = game.getMap().at(pos);

    if (tile.getSettlementType() != SettlementTypeEnum::Starfish) return;
    // Nagroda
    PlayerSystem::addStars(game, UnitSystem::getOwnerId(game, unitId), kStarfishReward);

    // Usuń starfish z mapy, żeby nie dało się zebrać drugi raz
    tile.setResource(ResourcesEnum::None);
}

void InteractionSystem::handleRuin(Game& game, UnitId unitId, Pos pos) {
    if (!UnitSystem::unitExists(game, unitId)) return;

    if (!game.getMap().inBounds(pos)) return;

    // Must be standing on the ruin tile.
    const Pos up = UnitSystem::getPos(game, unitId);
    if (up.x != pos.x || up.y != pos.y) return;

    // Same action gating as village/city capture: must start the turn here.
    if (UnitSystem::movedThisTurn(game, unitId) || UnitSystem::attackedThisTurn(game, unitId)) return;

    Tile& tile = game.getMap().at(pos);
    if (tile.getSettlementType() != SettlementTypeEnum::Ruin) return;

    const PlayerId pid = UnitSystem::getOwnerId(game, unitId);
    const RuinReward reward = rollRuinReward(game, pid, pos);

    std::cout << "[RuinReward] " << ruinRewardToString(reward) << std::endl;


    switch (reward) {
        case RuinReward::Stars:
            PlayerSystem::addStars(game, pid, kRuinStarsReward);
            MonumentSystem::onStarsUpdated(game, pid);

            break;

        case RuinReward::Tech: {
            // deterministic tech roll (different salt than reward roll)
            const uint32_t seed = static_cast<uint32_t>(game.getWorldSeed());
            const uint32_t tr = ruinRng(seed, pos, 0xC0FFEEu);

            TechId tech = TechDB::rollRandomObtainableTechThisRound(PlayerSystem::getTechs(game, pid), tr);
            if (tech != TechId::Count && !PlayerSystem::hasTech(game, pid, tech)) {
                PlayerSystem::addTech(game, pid, tech);
            } else {
                // fallback (should be rare): give stars if no tech available
                PlayerSystem::addStars(game, pid, kRuinStarsReward);
                MonumentSystem::onStarsUpdated(game, pid);

            }
            break;
        }

        case RuinReward::Population: {
            const CityId capId = PlayerSystem::getCapitalId(game, pid);
            if (capId != kNoCity && CitySystem::cityExists(game, capId)) {
                const uint8_t oldLevel = CitySystem::getCityLevel(game, capId);

                (void)CitySystem::addPopulation(game, capId, 3);

                const uint8_t newLevel = CitySystem::getCityLevel(game, capId);
                if (oldLevel < 5 && newLevel >= 5) {
                    // Call monument hook using the actual city position (not the ruin tile).
                    MonumentSystem::onCityReachedLevel5(game, CitySystem::getCityPos(game, capId));
                }
            } else {
                PlayerSystem::addStars(game, pid, kRuinStarsReward);
                MonumentSystem::onStarsUpdated(game, pid);
            }
            break;
        }

        case RuinReward::Explorer:
            VisionSystem::doExplorer(game, pid, pos);
            break;

        case RuinReward::VeteranUnit: {
            Tile& tile = game.getMap().at(pos);
            // Forced spawn: the new unit spawns on this tile, so push the unit currently standing here.
            // (This mirrors Polytopia-style forced spawns.)
            {
                const UnitId occ = game.getMap().unitOn(pos);
                if (occ != Map::kNoUnit) {
                    MovementSystem::forceMove(game, occ, pos);
                }
            }

            const bool isWater =
                tile.getBaseTerrain() == BaseTerrainEnum::Water ||
                tile.getBaseTerrain() == BaseTerrainEnum::Ocean;

            const PlayerId owner = pid;

            UnitId uid;
            if (isWater) {
                uid = UnitSpawnSystem::spawnUnitForced(
                    game, game.getMap(),
                    UnitType::Rammer,
                    owner,
                    pos,
                    /*canActImmediately=*/false, // free but cannot act this turn
                    /*makeVeteran=*/true
                );

            } else {
                uid = UnitSpawnSystem::spawnUnitForced(
                    game, game.getMap(),
                    UnitType::Swordsman,
                    owner,
                    pos,
                    /*canActImmediately=*/false, // free but cannot act this turn
                    /*makeVeteran=*/true
                );
            }

            // Removed manual blocking of unit turn; spawnUnitForced already sets flags.

            break;
        }
    }
            // Consumes the whole turn.
    tile.setResource(ResourcesEnum::None);
    tile.setSettlement(SettlementTypeEnum::None, kNoSettlement);
    UnitSystem::setAttackedThisTurn(game, unitId, true);
    UnitSystem::setMovedThisTurn(game, unitId, true);
}

InteractionSystem::RuinReward InteractionSystem::rollRuinReward(Game& game, PlayerId pid, Pos ruinPos) {
    const uint32_t seed = static_cast<uint32_t>(game.getWorldSeed());
    const uint32_t r = ruinRng(seed, ruinPos, 0xBEEF);

    std::vector<RuinReward> pool;
    pool.reserve(5);

    // zawsze
    pool.push_back(RuinReward::Stars);

    // tech jeśli jest coś do zbadania
    for (uint8_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        TechId t = static_cast<TechId>(i);
        if (!PlayerSystem::hasTech(game, pid, t)) {
            TechId pre = TechDB::getPrerequisite(t);
            if (pre == TechId::Count || PlayerSystem::hasTech(game, pid, pre)) {
                pool.push_back(RuinReward::Tech);
                break;
            }
        }
    }

    // population jeśli jest stolica
    if (PlayerSystem::getCapitalId(game, pid) != kNoCity) {
        pool.push_back(RuinReward::Population);
    }

    // explorer jeśli coś nieodkryte w 5x5

    pool.push_back(RuinReward::Explorer);


    // jednostka (zawsze jako opcja)
    pool.push_back(RuinReward::VeteranUnit);

    return pool[r % pool.size()];
}



void InteractionSystem::handleVillage(Game& game, UnitId unitId, Pos pos) {
    if (!UnitSystem::unitExists(game, unitId)) return;

    Tile& tile = game.getMap().at(pos);
    if (UnitSystem::movedThisTurn(game, unitId) || UnitSystem::attackedThisTurn(game, unitId)) return;

    if (tile.getSettlementType() != SettlementTypeEnum::Village) return;

    // Convert Village -> City on this tile
    const PlayerId owner = UnitSystem::getOwnerId(game, unitId);

    // 1) Change tile settlement type (id is assigned in foundCityFromVillage)
    tile.setSettlement(SettlementTypeEnum::City, kNoSettlement);

    // 2) Create/register City for this tile
    game.foundCityFromVillage(owner, pos);

    // 3) Consumes the whole turn: unit cannot move anymore this round
    UnitSystem::setMovedThisTurn(game, unitId, true);
    UnitSystem::setAttackedThisTurn(game, unitId, true);
}

void InteractionSystem::handleCityCapture(Game& game, UnitId unitId, Pos pos) {
    if (!UnitSystem::unitExists(game, unitId)) return;

    if (!game.getMap().inBounds(pos)) return;

    Tile& tile = game.getMap().at(pos);
    if (tile.getSettlementType() != SettlementTypeEnum::City) return;

    // Only allow capture if the unit is standing on the city tile.
    const Pos up = UnitSystem::getPos(game, unitId);
    if (up.x != pos.x || up.y != pos.y) return;

    // Capturing consumes the whole turn.
    if (UnitSystem::movedThisTurn(game, unitId) || UnitSystem::attackedThisTurn(game, unitId)) return;

    const PlayerId capturer = UnitSystem::getOwnerId(game, unitId);
    if (!game.captureCityAt(capturer, pos)) return;

    UnitSystem::setMovedThisTurn(game, unitId, true);
    UnitSystem::setAttackedThisTurn(game, unitId, true);

}
