//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "InteractionSystem.h"

#include "Game.h"
#include "World/Map.h"
#include "World/Tile.h"
#include "World/Settlements/City.h"
#include "Player/Player.h"
#include "Systems/MonumentSystem.h"

#include "terrain/ResourcesEnum.h"
#include "terrain/SettlementTypeEnum.h"
#include "Systems/RewardSystem.h"
#include "Systems/VisionSystem.h"
#include "tech/TechDB.h"
#include "terrain/VisibilityEnum.h"
#include "UnitFactory.h"

#include <cstdint>
#include <iostream>
#include <ostream>
#include <vector>

#include "UnitSpawnSystem.h"
#include "Systems/MovementSystem.h"  // Added include as per instructions



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
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

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
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    Tile& tile = game.getMap().at(pos);

    if (tile.getSettlementType() != SettlementTypeEnum::Starfish) return;
    // Nagroda
    game.getPlayer(unit->getOwnerId()).addStars(kStarfishReward);

    // Usuń starfish z mapy, żeby nie dało się zebrać drugi raz
    tile.setResource(ResourcesEnum::None);
}

void InteractionSystem::handleRuin(Game& game, UnitId unitId, Pos pos) {
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    if (!game.getMap().inBounds(pos)) return;

    // Must be standing on the ruin tile.
    const Pos up = unit->getPos();
    if (up.x != pos.x || up.y != pos.y) return;

    // Same action gating as village/city capture: must start the turn here.
    if (unit->movedThisTurn() || unit->attackedThisTurn()) return;

    Tile& tile = game.getMap().at(pos);
    if (tile.getSettlementType() != SettlementTypeEnum::Ruin) return;

    Player& pl = game.getPlayer(unit->getOwnerId());
    const RuinReward reward = rollRuinReward(game, pl, pos);

    std::cout << "[RuinReward] " << ruinRewardToString(reward) << std::endl;


    switch (reward) {
        case RuinReward::Stars:
            pl.addStars(kRuinStarsReward);
            MonumentSystem::onStarsUpdated(game,unit->getOwnerId());

            break;

        case RuinReward::Tech: {
            // deterministic tech roll (different salt than reward roll)
            const uint32_t seed = static_cast<uint32_t>(game.getWorldSeed());
            const uint32_t tr = ruinRng(seed, pos, 0xC0FFEEu);

            TechId tech = TechDB::rollRandomObtainableTechThisRound(pl.getTechs(), tr);
            if (tech != TechId::Count && !pl.hasTech(tech)) {
                pl.addTech(tech);
            } else {
                // fallback (should be rare): give stars if no tech available
                pl.addStars(kRuinStarsReward);
                MonumentSystem::onStarsUpdated(game,unit->getOwnerId());

            }
            break;
        }

        case RuinReward::Population: {
            if (City* cap = game.getCity(pl.getCapitalId())) {
                cap->addPopulation(3);
                MonumentSystem::onCityReachedLevel5(game,pos);
            } else {
                pl.addStars(kRuinStarsReward);
                MonumentSystem::onStarsUpdated(game,unit->getOwnerId());

            }
            break;
        }

        case RuinReward::Explorer:
            VisionSystem::doExplorer(game, unit->getOwnerId(), pos);
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

            const PlayerId owner = pl.getId();

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
    unit->setAttackedThisTurn(true);

    unit->setMovedThisTurn(true);
}

InteractionSystem::RuinReward InteractionSystem::rollRuinReward(Game& game, Player& player, Pos ruinPos) {
    const uint32_t seed = static_cast<uint32_t>(game.getWorldSeed());
    const uint32_t r = ruinRng(seed, ruinPos, 0xBEEF);

    std::vector<RuinReward> pool;
    pool.reserve(5);

    // zawsze
    pool.push_back(RuinReward::Stars);

    // tech jeśli jest coś do zbadania
    for (uint8_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        TechId t = static_cast<TechId>(i);
        if (!player.hasTech(t)) {
            TechId pre = TechDB::getPrerequisite(t);
            if (pre == TechId::Count || player.hasTech(pre)) {
                pool.push_back(RuinReward::Tech);
                break;
            }
        }
    }

    // population jeśli jest stolica
    if (player.getCapitalId() != kNoCity) {
        pool.push_back(RuinReward::Population);
    }

    // explorer jeśli coś nieodkryte w 5x5

    pool.push_back(RuinReward::Explorer);


    // jednostka (zawsze jako opcja)
    pool.push_back(RuinReward::VeteranUnit);

    return pool[r % pool.size()];
}



void InteractionSystem::handleVillage(Game& game, UnitId unitId, Pos pos) {
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    Tile& tile = game.getMap().at(pos);
    if (unit->movedThisTurn() || unit->attackedThisTurn()) return;

    if (tile.getSettlementType() != SettlementTypeEnum::Village) return;

    // Convert Village -> City on this tile
    const PlayerId owner = unit->getOwnerId();

    // 1) Change tile settlement type (id is assigned in foundCityFromVillage)
    tile.setSettlement(SettlementTypeEnum::City, kNoSettlement);

    // 2) Create/register City for this tile
    game.foundCityFromVillage(owner, pos);

    // 3) Consumes the whole turn: unit cannot move anymore this round
    unit->setMovedThisTurn(true);
    unit->setAttackedThisTurn(true);
}

void InteractionSystem::handleCityCapture(Game& game, UnitId unitId, Pos pos) {
    Unit* unit = game.getUnit(unitId);
    if (!unit) return;

    if (!game.getMap().inBounds(pos)) return;

    Tile& tile = game.getMap().at(pos);
    if (tile.getSettlementType() != SettlementTypeEnum::City) return;

    // Only allow capture if the unit is standing on the city tile.
    const Pos up = unit->getPos();
    if (up.x != pos.x || up.y != pos.y) return;

    // Capturing consumes the whole turn.
    if (unit->movedThisTurn() || unit->attackedThisTurn()) return;

    const PlayerId capturer = unit->getOwnerId();
    if (!game.captureCityAt(capturer, pos)) return;

    unit->setMovedThisTurn(true);
    unit->setAttackedThisTurn(true);

}
