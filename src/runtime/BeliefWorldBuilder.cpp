#include "runtime/BeliefWorldBuilder.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "content/units/UnitFactory.h"
#include "game/Game.h"

namespace {
constexpr size_t kBeliefTokenWidth = 23;
constexpr int kUnitTypeMax = static_cast<int>(UnitType::GiantSuper);

// `Game::worldSeed` drives deterministic random outcomes such as ruins.  A
// detached belief world must never inherit that value: it could otherwise
// reveal facts that are not represented in the player's observation.  Keep a
// stable, explicitly byte-oriented hash here instead of std::hash so that a
// saved belief hypothesis produces the same rollout seed on every supported
// platform.
constexpr uint64_t kBeliefSeedHashOffset = 14695981039346656037ull;
constexpr uint64_t kBeliefSeedHashPrime = 1099511628211ull;
constexpr uint64_t kBeliefSeedDomain = 0x504F4C5942454C31ull; // "POLYBEL1"

void hashByte(uint64_t& hash, uint8_t value) {
    hash ^= value;
    hash *= kBeliefSeedHashPrime;
}

void hashU32(uint64_t& hash, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hashByte(hash, static_cast<uint8_t>(value >> shift));
    }
}

void hashU64(uint64_t& hash, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hashByte(hash, static_cast<uint8_t>(value >> shift));
    }
}

uint64_t avalanche64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

uint32_t deriveBeliefWorldSeed(const Game& source,
                                PlayerId perspective,
                                const int32_t* tokens,
                                size_t rowCount,
                                size_t columnCount) {
    uint64_t hash = kBeliefSeedHashOffset;
    hashU64(hash, kBeliefSeedDomain);
    hashU32(hash, 1u); // derivation format version

    // This metadata is part of the public game position.  In particular, do
    // not read source.getWorldSeed() or source-map contents here.
    const Map& map = source.getMap();
    hashU32(hash, static_cast<uint32_t>(map.getWidth()));
    hashU32(hash, static_cast<uint32_t>(map.getHeight()));
    hashU32(hash, static_cast<uint32_t>(perspective));
    hashU32(hash, static_cast<uint32_t>(source.getCurrentPlayerId()));
    hashU32(hash, source.getTurnNumber());
    hashU32(hash, static_cast<uint32_t>(source.getWinner()));

    const std::vector<Player>& players = source.getPlayers();
    hashU64(hash, static_cast<uint64_t>(players.size()));
    for (const Player& player : players) {
        hashU32(hash, static_cast<uint32_t>(player.getTribeType()));
    }

    // The completed token matrix is the explicit belief hypothesis.  Hash
    // signed values through their uint32_t representation and feed bytes in a
    // fixed order, rather than hashing native memory with host endianness.
    hashU64(hash, static_cast<uint64_t>(rowCount));
    hashU64(hash, static_cast<uint64_t>(columnCount));
    const size_t valueCount = rowCount * columnCount;
    for (size_t index = 0; index < valueCount; ++index) {
        hashU32(hash, static_cast<uint32_t>(tokens[index]));
    }

    const uint64_t mixed = avalanche64(hash);
    uint32_t seed = static_cast<uint32_t>(mixed) ^ static_cast<uint32_t>(mixed >> 32);
    // Preserve Game's non-zero effective-seed invariant.
    return seed == 0 ? 1u : seed;
}

bool inRange(int value, int minimum, int maximum) {
    return value >= minimum && value <= maximum;
}

void validateFlatTokens(const Game& source,
                        PlayerId perspective,
                        const int32_t* tokens,
                        size_t rowCount,
                        size_t columnCount) {
    const Map& map = source.getMap();
    const size_t expected = static_cast<size_t>(map.getWidth()) * static_cast<size_t>(map.getHeight());
    if (rowCount != expected) throw std::invalid_argument("completed_map_tokens has an invalid tile count");
    if (columnCount != kBeliefTokenWidth) {
        throw std::invalid_argument("each completed_map_tokens row must have exactly 23 values");
    }
    if (!tokens && rowCount != 0) throw std::invalid_argument("completed_map_tokens data is null");
    if (perspective == kNoPlayer || static_cast<size_t>(perspective) >= source.getPlayers().size())
        throw std::invalid_argument("belief perspective is not a valid player");

    std::vector<int> cityCenters;
    for (size_t i = 0; i < rowCount; ++i) {
        const int32_t* t = tokens + i * columnCount;
        if (!inRange(t[0], 0, 1) || !inRange(t[7], 0, 3) || !inRange(t[8], 0, 16) ||
            !inRange(t[11], -1, 1) || !inRange(t[14], 0, 4) || !inRange(t[18], 0, 6) ||
            !inRange(t[19], 0, 4) || !inRange(t[20], 0, 16))
            throw std::invalid_argument("completed_map_tokens contains an invalid tile enum");

        const bool hasUnit = t[4] != -1;
        if (hasUnit) {
            if (!inRange(t[4], 1, kUnitTypeMax) || t[3] < 0 ||
                static_cast<size_t>(t[3]) >= source.getPlayers().size() || t[2] < 0 ||
                t[21] < t[2] || t[22] < -1)
                throw std::invalid_argument("completed_map_tokens contains an invalid unit state");
        } else if (t[2] != -1 || t[3] != -1 || t[21] != -1 || t[22] != -1) {
            throw std::invalid_argument("unit fields must be -1 when unit_type is -1");
        }

        if (t[14] == static_cast<int>(SettlementTypeEnum::City)) {
            if (t[13] < 1 || t[15] < 0 || t[15] > std::numeric_limits<CityId>::max() ||
                t[16] < 0 || static_cast<size_t>(t[16]) >= source.getPlayers().size() ||
                !inRange(t[10], 0, 1) || !inRange(t[11], 0, 1) || t[12] < 0)
                throw std::invalid_argument("completed_map_tokens contains an invalid city state");
            if (t[15] >= static_cast<int>(cityCenters.size())) cityCenters.resize(static_cast<size_t>(t[15]) + 1, -1);
            if (cityCenters[static_cast<size_t>(t[15])] != -1)
                throw std::invalid_argument("a city id may appear on only one city-center tile");
            cityCenters[static_cast<size_t>(t[15])] = static_cast<int>(i);
        } else if (t[10] != -1 || t[11] != -1 || t[12] != -1 || t[13] != -1 ||
                   t[16] != -1 || t[17] != -1) {
            throw std::invalid_argument("city fields must be -1 outside a city tile");
        }
    }
    for (size_t i = 0; i < rowCount; ++i) {
        const int32_t* t = tokens + i * columnCount;
        for (const int cityId : {t[6], t[22]}) {
            if (cityId >= 0 && (static_cast<size_t>(cityId) >= cityCenters.size() ||
                cityCenters[static_cast<size_t>(cityId)] < 0)) {
                throw std::invalid_argument("city references must refer to a city-center token");
            }
        }
        if (t[6] < -1) throw std::invalid_argument("territory_city_id is invalid");
    }
}
} // namespace

Game BeliefWorldBuilder::build(const Game& observedSource, PlayerId perspective,
                               const std::vector<std::vector<int>>& tokens) {
    const size_t rowCount = tokens.size();
    std::vector<int32_t> flat;
    flat.reserve(rowCount * kBeliefTokenWidth);
    for (const std::vector<int>& row : tokens) {
        if (row.size() != kBeliefTokenWidth) {
            throw std::invalid_argument("each completed_map_tokens row must have exactly 23 values");
        }
        for (const int value : row) flat.push_back(static_cast<int32_t>(value));
    }
    return buildFlat(observedSource, perspective, flat.data(), rowCount, kBeliefTokenWidth);
}

Game BeliefWorldBuilder::buildFlat(const Game& observedSource,
                                   PlayerId perspective,
                                   const int32_t* tokens,
                                   size_t rowCount,
                                   size_t columnCount) {
    validateFlatTokens(observedSource, perspective, tokens, rowCount, columnCount);
    const Map& sourceMap = observedSource.map;
    const int width = sourceMap.getWidth();
    const int height = sourceMap.getHeight();
    Game belief;
    belief.worldSeed = deriveBeliefWorldSeed(
        observedSource, perspective, tokens, rowCount, columnCount);
    belief.map.init(width, height);
    belief.map.setActiveTribes(sourceMap.getActiveTribes());

    belief.players.reserve(observedSource.players.size());
    for (size_t i = 0; i < observedSource.players.size(); ++i) {
        const Player& sourcePlayer = observedSource.players[i];
        belief.players.emplace_back(static_cast<PlayerId>(i), sourcePlayer.getTribeType(), 0, kNoCity);
        if (static_cast<PlayerId>(i) == perspective) {
            belief.players.back().addStars(sourcePlayer.getStars());
            for (TechId tech : sourcePlayer.getTechs()) belief.players.back().addTech(tech);
        }
    }
    // Pending city choices and lighthouse discoveries belong to the owning
    // player's private runtime.  They are required for an exact legal-action
    // reconstruction when a redeterminized ISMCTS branch returns to that
    // player, but copying another player's entries would leak information.
    for (const Game::PendingCityUpgrade& pending : observedSource.pendingCityUpgrades) {
        if (pending.pid == perspective) belief.pendingCityUpgrades.push_back(pending);
    }
    for (size_t lighthouse = 0; lighthouse < belief.lighthouseDiscoveredBy.size(); ++lighthouse) {
        const uint16_t bit = static_cast<uint16_t>(uint16_t(1) << perspective);
        if ((observedSource.lighthouseDiscoveredBy[lighthouse] & bit) != 0) {
            belief.lighthouseDiscoveredBy[lighthouse] |= bit;
        }
    }

    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int32_t* v = tokens + static_cast<size_t>(y * width + x) * columnCount;
        Tile& tile = belief.map.at(Pos{x, y});
        tile.setRoadBridge(static_cast<RoadBridgeEnum>(v[7]));
        tile.setBuildingType(static_cast<BuildingTypeEnum>(v[8]));
        tile.setSettlement(static_cast<SettlementTypeEnum>(v[14]),
                           v[15] < 0 ? kNoSettlement : static_cast<SettlementId>(v[15]));
        tile.setResource(static_cast<ResourcesEnum>(v[18]));
        tile.setBaseTerrain(static_cast<BaseTerrainEnum>(v[19]));
        tile.setTribe(static_cast<TribeType>(v[20]));
        tile.setTerritoryCityId(v[6] < 0 ? kNoCity : static_cast<CityId>(v[6]));
        if (v[0] == 1) tile.setVisibility(static_cast<VisibilityEnum>(uint16_t(1) << perspective));
    }

    int maxCityId = -1;
    for (size_t tile = 0; tile < rowCount; ++tile) {
        const int32_t* v = tokens + tile * columnCount;
        if (v[14] == static_cast<int>(SettlementTypeEnum::City)) {
            maxCityId = std::max(maxCityId, static_cast<int>(v[15]));
        }
    }
    belief.cities.resize(static_cast<size_t>(maxCityId + 1));
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int32_t* v = tokens + static_cast<size_t>(y * width + x) * columnCount;
        if (v[14] != static_cast<int>(SettlementTypeEnum::City)) continue;
        const CityId cityId = static_cast<CityId>(v[15]);
        City& city = belief.cities[cityId];
        city.setCityId(cityId); city.setId(cityId); city.setPos(Pos{x, y});
        city.setOwnerId(static_cast<PlayerId>(v[16])); city.setLevel(static_cast<uint8_t>(v[13]));
        city.setCapital(v[9] == 1); city.setWorkshopEnabled(v[10] == 1);
        city.setCityWallEnabled(v[11] == 1); city.setParkCount(static_cast<uint8_t>(v[12]));
        Player& owner = belief.players[static_cast<size_t>(v[16])];
        owner.addCity(cityId);
        if (v[9] == 1) owner.setCapitalId(cityId);
        if (static_cast<PlayerId>(v[16]) == perspective) {
            if (const City* sourceCity = observedSource.getCity(cityId)) {
                city.setPopulation(sourceCity->getPopulation());
                city.setIsInfiltrated(sourceCity->getIsInfiltrated());
            }
        }
    }

    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const Pos pos{x, y};
        const int32_t* v = tokens + static_cast<size_t>(y * width + x) * columnCount;
        if (v[4] == -1) continue;
        const UnitId unitId = static_cast<UnitId>(belief.units.size());
        Unit unit = UnitFactory::create(static_cast<UnitType>(v[4]), static_cast<PlayerId>(v[3]), pos);
        unit.setId(unitId); unit.setHealth(v[2]); unit.setMaxHealth(v[21]);
        if (v[22] >= 0) unit.setOriginCityId(static_cast<CityId>(v[22]));

        const UnitId sourceId = sourceMap.unitOn(pos);
        if (static_cast<PlayerId>(v[3]) == perspective && sourceId != Map::kNoUnit) {
            if (const Unit* sourceUnit = observedSource.getUnit(sourceId);
                sourceUnit && sourceUnit->getOwnerId() == perspective &&
                static_cast<int>(sourceUnit->getType()) == v[4]) {
                unit = *sourceUnit;
                // An observed enemy attack can change health without changing
                // the rest of an owned unit's runtime (skills, movement,
                // veteran state, etc.). Preserve that private runtime while
                // applying the public/own token values.
                unit.setId(unitId); unit.setPos(pos);
                unit.setHealth(v[2]); unit.setMaxHealth(v[21]);
                if (v[22] >= 0) unit.setOriginCityId(static_cast<CityId>(v[22]));
                if (v[5] >= 0) unit.setKillCounter(v[5]);
            }
        }
        belief.units.push_back(unit);
        belief.map.setUnitOn(pos, unitId);
        belief.players[static_cast<size_t>(v[3])].addUnit(unitId);
        // For another player's information set, origin is the best available
        // hypothesis about a unit's city membership.  For the perspective's
        // own units it is not: units can be reassigned between cities while
        // retaining their immutable origin-city id.  The public city token
        // carries the authoritative current occupancy in column 17 and is
        // restored exactly below.
        if (v[22] >= 0 && static_cast<PlayerId>(v[3]) != perspective) {
            belief.cities[static_cast<size_t>(v[22])].addUnit(unitId);
        }
    }

    // ``city_units_occupied`` is visible to the city owner and determines
    // whether a city can train another unit.  Reconstruct the exact count for
    // the perspective without inventing map units: these sentinel ids occupy
    // only City::units capacity slots, never the map or Player::units list.
    // This is necessary because ``unit_origin_city`` describes origin, not a
    // unit's current city assignment.
    UnitId syntheticOccupancyId = static_cast<UnitId>(kNoUnit - 1);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int32_t* v = tokens + static_cast<size_t>(y * width + x) * columnCount;
        if (v[14] != static_cast<int>(SettlementTypeEnum::City) ||
            static_cast<PlayerId>(v[16]) != perspective || v[17] < 0) {
            continue;
        }
        City& city = belief.cities[static_cast<size_t>(v[15])];
        const int desiredOccupancy = std::max(0, v[17]);
        for (int occupied = 0; occupied < desiredOccupancy; ++occupied) {
            if (syntheticOccupancyId == kNoUnit) {
                throw std::invalid_argument("belief city occupancy exceeds synthetic unit id capacity");
            }
            city.addUnit(syntheticOccupancyId--);
        }
    }

    belief.currentPlayer = observedSource.currentPlayer;
    belief.turnNumber = observedSource.turnNumber;
    belief.winner = observedSource.winner;
    belief.resetVisibilityCache(belief.players.size());
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int32_t* v = tokens + static_cast<size_t>(y * width + x) * columnCount;
        if (v[0] == 1) belief.noteTileVisible(perspective, Pos{x, y});
    }
    return belief;
}
