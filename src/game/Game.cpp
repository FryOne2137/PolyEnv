//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#include "Game.h"
#include "../content/tribes/Tribe.h"
#include "../content/tech/TechDB.h"

#include "../systems/TurnSystem.h"
#include "../systems/MovementSystem.h"
#include "../systems/CombatSystem.h"
#include "../systems/InteractionSystem.h"
#include "../systems/VisionSystem.h"
#include "../systems/BuildingSystem.h"
#include "../systems/TechSystem.h"
#include "../systems/PlayerSystem.h"
#include "../systems/UnitSpawnSystem.h"
#include "../systems/TileActionSystem.h"
#include "../systems/GameDataSystem.h"
#include <random>
#include <stdexcept>

#include "../systems/ScoreSystem.h"
#include "../systems/UnitSystem.h"
#include "../systems/CitySystem.h"

#include "../systems/UnitUpgradeSystem.h"
#include "../systems/DisbandSystem.h"
#include "../systems/CityRewardSystem.h"
#include "../content/skills/UnitSkill.h"

#include <algorithm>
#include <limits>

#include "../world/Tile.h"
#include "terrain/SettlementId.h"

namespace {
    static bool canCurrentPlayerAct(const Game& game, PlayerId pid) {
        return !game.isGameOver() && game.isPlayersTurn(pid) && !game.hasPendingCityUpgrade(pid);
    }

    static bool canControlUnit(const Game& game, PlayerId pid, UnitId unitId, bool requireActionSlot) {
        if (!UnitSystem::unitExists(game, unitId)) return false;
        if (UnitSystem::getOwnerId(game, unitId) != pid) return false;
        if (requireActionSlot) return canCurrentPlayerAct(game, pid);
        return game.isPlayersTurn(pid);
    }

}

#if 0 // Moved to runtime/BeliefWorldBuilder.cpp
namespace {
constexpr size_t kBeliefTokenWidth = 23;
constexpr int kUnitTypeMax = static_cast<int>(UnitType::GiantSuper);

bool tokenEnumInRange(int value, int minValue, int maxValue) {
    return value >= minValue && value <= maxValue;
}

void validateCompletedBeliefTokens(const Game& source, PlayerId perspective,
                                   const std::vector<std::vector<int>>& tokens) {
    const Map& map = source.getMap();
    const size_t expected = static_cast<size_t>(map.getWidth()) * static_cast<size_t>(map.getHeight());
    if (tokens.size() != expected) throw std::invalid_argument("completed_map_tokens has an invalid tile count");
    if (perspective == kNoPlayer || static_cast<size_t>(perspective) >= source.getPlayers().size())
        throw std::invalid_argument("belief perspective is not a valid player");

    std::vector<int> cityCenters;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& t = tokens[i];
        if (t.size() != kBeliefTokenWidth)
            throw std::invalid_argument("each completed_map_tokens row must have exactly 23 values");
        if (!tokenEnumInRange(t[0], 0, 1) || !tokenEnumInRange(t[7], 0, 3) ||
            !tokenEnumInRange(t[8], 0, 16) || !tokenEnumInRange(t[11], -1, 1) ||
            !tokenEnumInRange(t[14], 0, 4) || !tokenEnumInRange(t[18], 0, 6) ||
            !tokenEnumInRange(t[19], 0, 4) || !tokenEnumInRange(t[20], 0, 16))
            throw std::invalid_argument("completed_map_tokens contains an invalid tile enum");

        const bool hasUnit = t[4] != -1;
        if (hasUnit) {
            if (!tokenEnumInRange(t[4], 1, kUnitTypeMax) ||
                t[3] < 0 || static_cast<size_t>(t[3]) >= source.getPlayers().size() ||
                t[2] < 0 || t[21] < t[2] || t[22] < -1)
                throw std::invalid_argument("completed_map_tokens contains an invalid unit state");
        } else if (t[2] != -1 || t[3] != -1 || t[21] != -1 || t[22] != -1) {
            throw std::invalid_argument("unit fields must be -1 when unit_type is -1");
        }

        if (t[14] == static_cast<int>(SettlementTypeEnum::City)) {
            if (t[13] < 1 || t[15] < 0 || t[15] > std::numeric_limits<CityId>::max() ||
                t[16] < 0 || static_cast<size_t>(t[16]) >= source.getPlayers().size() ||
                !tokenEnumInRange(t[10], 0, 1) || !tokenEnumInRange(t[11], 0, 1) || t[12] < 0)
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
    for (size_t i = 0; i < tokens.size(); ++i) {
        const int territoryCity = tokens[i][6];
        if (territoryCity < -1 || (territoryCity >= 0 &&
            (static_cast<size_t>(territoryCity) >= cityCenters.size() || cityCenters[static_cast<size_t>(territoryCity)] < 0)))
            throw std::invalid_argument("territory_city_id must refer to a city-center token");
        const int originCity = tokens[i][22];
        if (originCity >= 0 && (static_cast<size_t>(originCity) >= cityCenters.size() ||
            cityCenters[static_cast<size_t>(originCity)] < 0))
            throw std::invalid_argument("unit_origin_city_id must refer to a city-center token");
    }
}
} // namespace

Game Game::fromBeliefTokens(const Game& observedSource, PlayerId perspective,
                            const std::vector<std::vector<int>>& tokens) {
    validateCompletedBeliefTokens(observedSource, perspective, tokens);
    const Map& sourceMap = observedSource.map;
    const int width = sourceMap.getWidth();
    const int height = sourceMap.getHeight();
    Game belief;
    belief.worldSeed = observedSource.worldSeed;
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

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y * width + x);
            const auto& v = tokens[index];
            Tile& tile = belief.map.at(Pos{x, y});
            tile.setRoadBridge(static_cast<RoadBridgeEnum>(v[7]));
            tile.setBuildingType(static_cast<BuildingTypeEnum>(v[8]));
            tile.setSettlement(static_cast<SettlementTypeEnum>(v[14]),
                               v[15] < 0 ? kNoSettlement : static_cast<SettlementId>(v[15]));
            tile.setResource(static_cast<ResourcesEnum>(v[18]));
            tile.setBaseTerrain(static_cast<BaseTerrainEnum>(v[19]));
            tile.setTribe(static_cast<TribeType>(v[20]));
            tile.setTerritoryCityId(v[6] < 0 ? kNoCity : static_cast<CityId>(v[6]));
            if (v[0] == 1) {
                tile.setVisibility(static_cast<VisibilityEnum>(uint16_t(1) << perspective));
            }
        }
    }

    int maxCityId = -1;
    for (const auto& v : tokens) if (v[14] == static_cast<int>(SettlementTypeEnum::City)) maxCityId = std::max(maxCityId, v[15]);
    belief.cities.resize(static_cast<size_t>(maxCityId + 1));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto& v = tokens[static_cast<size_t>(y * width + x)];
            if (v[14] != static_cast<int>(SettlementTypeEnum::City)) continue;
            const CityId cityId = static_cast<CityId>(v[15]);
            City& city = belief.cities[cityId];
            city.setCityId(cityId);
            city.setId(cityId);
            city.setPos(Pos{x, y});
            city.setOwnerId(static_cast<PlayerId>(v[16]));
            city.setLevel(static_cast<uint8_t>(v[13]));
            city.setCapital(v[9] == 1);
            city.setWorkshopEnabled(v[10] == 1);
            city.setCityWallEnabled(v[11] == 1);
            city.setParkCount(static_cast<uint8_t>(v[12]));
            Player& owner = belief.players[static_cast<size_t>(v[16])];
            owner.addCity(cityId);
            if (v[9] == 1) owner.setCapitalId(cityId);

            // Only the perspective player's non-map city runtime state is
            // copied.  It is their own information, never hidden opponent data.
            if (static_cast<PlayerId>(v[16]) == perspective) {
                if (const City* sourceCity = observedSource.getCity(cityId)) {
                    city.setPopulation(sourceCity->getPopulation());
                    city.setIsInfiltrated(sourceCity->getIsInfiltrated());
                }
            }
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Pos pos{x, y};
            const auto& v = tokens[static_cast<size_t>(y * width + x)];
            if (v[4] == -1) continue;
            const UnitId unitId = static_cast<UnitId>(belief.units.size());
            Unit unit = UnitFactory::create(static_cast<UnitType>(v[4]), static_cast<PlayerId>(v[3]), pos);
            unit.setId(unitId);
            unit.setHealth(v[2]);
            unit.setMaxHealth(v[21]);
            if (v[22] >= 0) unit.setOriginCityId(static_cast<CityId>(v[22]));

            // Preserve full runtime state only for a matching own unit.  A
            // predicted opponent is always a fresh hypothesis.
            const UnitId sourceId = sourceMap.unitOn(pos);
            if (static_cast<PlayerId>(v[3]) == perspective && sourceId != Map::kNoUnit) {
                if (const Unit* sourceUnit = observedSource.getUnit(sourceId);
                    sourceUnit && sourceUnit->getOwnerId() == perspective &&
                    static_cast<int>(sourceUnit->getType()) == v[4] &&
                    sourceUnit->getHealth() == v[2] && sourceUnit->getMaxHealth() == v[21]) {
                    unit = *sourceUnit;
                    unit.setId(unitId);
                    unit.setPos(pos);
                }
            }
            belief.units.push_back(unit);
            belief.map.setUnitOn(pos, unitId);
            belief.players[static_cast<size_t>(v[3])].addUnit(unitId);
            if (v[22] >= 0) belief.cities[static_cast<size_t>(v[22])].addUnit(unitId);
        }
    }

    belief.currentPlayer = observedSource.currentPlayer;
    belief.turnNumber = observedSource.turnNumber;
    belief.winner = observedSource.winner;
    belief.resetVisibilityCache(belief.players.size());
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (tokens[static_cast<size_t>(y * width + x)][0] == 1) belief.noteTileVisible(perspective, Pos{x, y});
    }
    return belief;
}

#endif

// Game.cpp
City* Game::getCity(CityId id) {
    if (id == kNoCity || id >= cities.size()) return nullptr;
    return &cities[id];
}

const City* Game::getCity(CityId id) const {
    if (id == kNoCity || id >= cities.size()) return nullptr;
    return &cities[id];
}

const std::vector<Pos>& Game::getVisibleTiles(PlayerId pid) const {
    static const std::vector<Pos> empty;
    if (pid == kNoPlayer) return empty;
    const size_t idx = static_cast<size_t>(pid);
    if (idx >= visibleTilesByPlayer.size()) return empty;
    return visibleTilesByPlayer[idx];
}

bool Game::isTileVisibleForPlayer(PlayerId pid, Pos p) const {
    if (pid == kNoPlayer) return false;
    if (!map.inBounds(p)) return false;
    const size_t idx = static_cast<size_t>(pid);
    if (idx >= visibleLookupByPlayer.size()) return false;
    const int flat = map.index(p);
    if (flat < 0) return false;
    const size_t sflat = static_cast<size_t>(flat);
    if (sflat >= visibleLookupByPlayer[idx].size()) return false;
    return visibleLookupByPlayer[idx][sflat] != 0;
}

ResourcesEnum Game::getVisibleResourceForPlayer(PlayerId pid, Pos p) const {
    if (pid == kNoPlayer || !map.inBounds(p) || static_cast<size_t>(pid) >= players.size()) {
        return ResourcesEnum::None;
    }

    const ResourcesEnum resource = map.at(p).getResource();
    const Player& player = players[static_cast<size_t>(pid)];
    if (resource == ResourcesEnum::Metal && !player.hasTech(TechId::Climbing)) {
        return ResourcesEnum::None;
    }
    if (resource == ResourcesEnum::Crops &&
        !player.hasTech(TechId::Organization) && !player.hasTech(TechId::Farming)) {
        return ResourcesEnum::None;
    }
    return resource;
}

void Game::noteTileVisible(PlayerId pid, Pos p) {
    if (pid == kNoPlayer) return;
    if (!map.inBounds(p)) return;
    const size_t idx = static_cast<size_t>(pid);
    if (idx >= visibleLookupByPlayer.size() || idx >= visibleTilesByPlayer.size()) return;
    const int flat = map.index(p);
    if (flat < 0) return;
    const size_t sflat = static_cast<size_t>(flat);
    if (sflat >= visibleLookupByPlayer[idx].size()) return;
    if (visibleLookupByPlayer[idx][sflat] != 0) return;

    visibleLookupByPlayer[idx][sflat] = 1;
    visibleTilesByPlayer[idx].push_back(p);
}

void Game::resetVisibilityCache(size_t playerCount) {
    const size_t cellCount = static_cast<size_t>(std::max(0, map.getWidth())) * static_cast<size_t>(std::max(0, map.getHeight()));
    visibleTilesByPlayer.assign(playerCount, {});
    for (auto& v : visibleTilesByPlayer) {
        v.reserve(cellCount / 4 + 16);
    }
    visibleLookupByPlayer.assign(playerCount, std::vector<uint8_t>(cellCount, 0));
}

void Game::newGame(const NewGameConfig& cfg) {
    citiesConnectionRuntime = {};

    // Load static game data (unit templates) before any units are spawned.
    if (!GameDataSystem::isUnitsLoaded()) {
        try {
            // Legacy fallback for native runs from repository root.
            GameDataSystem::loadUnits("data/Units.json");
        } catch (const std::exception&) {
            throw std::runtime_error(
                "GameDataSystem: Units.json is not loaded. "
                "Load it explicitly (e.g. pass units_json_path in Python) "
                "before starting a new game."
            );
        }
    }

    // 1) aktywne plemiona na mapie
    map.setActiveTribes(cfg.tribes);

    // 2) generacja mapy
    // Single source of truth for determinism.
    if (cfg.seed == 0) {
        std::random_device rd;
        worldSeed = (static_cast<uint32_t>(rd()) << 16) ^ static_cast<uint32_t>(rd());
        if (worldSeed == 0) worldSeed = 1u;
    } else {
        worldSeed = cfg.seed;
    }

    MapGenerator::Params p;
    p.mapSize = cfg.mapSize;
    p.mapType = cfg.mapType;
    p.seed = worldSeed;

    // Capitals are owned by this initialization call.  Keeping them local is
    // essential because several Game instances can generate maps concurrently.
    const auto capitals = MapGenerator::generate(map, p);

    cities.clear();
    cities.resize(cfg.tribes.size());

    for (size_t i = 0; i < cfg.tribes.size(); ++i) {
        const CityId cid = static_cast<CityId>(i);

        // Default city setup for all tribes
        (void)CitySystem::setCityId(*this, cid, cid);
        (void)CitySystem::setCityOwner(*this, cid, static_cast<uint8_t>(i));
        (void)CitySystem::setCityLevel(*this, cid, 1);
        (void)CitySystem::setCityPopulation(*this, cid, 0);
        (void)CitySystem::setCityStarsPerRound(*this, cid, 0);
        (void)CitySystem::setCityName(*this, cid, std::string("City ") + std::to_string(i));

        // IMPORTANT: store the city position (used by movement/connection systems).
        if (capitals.size() == cfg.tribes.size()) {
            (void)CitySystem::setCityPos(*this, cid, capitals[i]);
        }

        // Capitals generated by MapGenerator: each starting city is a capital
        (void)CitySystem::setCityCapital(*this, cid, true);

        // Special tribe rule: Luxidoor starts with a strong capital
        if (cfg.tribes[i] == TribeType::Luxidoor) {
            (void)CitySystem::setCityLevel(*this, cid, 2);
            (void)CitySystem::setCityStarsPerRound(*this, cid, 0);
            (void)CitySystem::setCityPopulation(*this, cid, 0);
        }
    }

    // 3) tworzymy graczy
    players.clear();
    players.reserve(cfg.tribes.size());

    for (size_t i = 0; i < cfg.tribes.size(); ++i) {
        PlayerId pid = static_cast<PlayerId>(i);

        // Startowe gwiazdki bierzemy z definicji plemienia (wiki defaults)
        const Tribe tribe = Tribe::makeDefault(cfg.tribes[i]);
        const int startStars = static_cast<int>(tribe.getStartStars());

        // capitalId is stable and matches MapGenerator (we use player index as capital/city id)
        const int capitalId = static_cast<int>(i);
        players.emplace_back(pid, cfg.tribes[i], startStars, capitalId);

        // Grant starting technology (new TechId-based tech system)
        const TechId startTech = tribe.getStartTech();
        if (startTech != TechId::Count) {
            PlayerSystem::addTech(*this, pid, startTech);
        }
    }

    resetVisibilityCache(players.size());

    // 3b) Initialize capitals (tile settlement + territory) via CitySystem.
    // Must happen after players are created, because initCapital also ensures player city list + capital id.
    if (capitals.size() == cfg.tribes.size()) {
        for (size_t i = 0; i < cfg.tribes.size(); ++i) {
            (void)CitySystem::initCapital(*this, static_cast<PlayerId>(i), static_cast<CityId>(i), capitals[i]);
        }
    }

    // 4) jednostki startowe
    units.clear();

    // 4b) spawn starting units on capitals (tribe default start unit)
    if (capitals.size() == cfg.tribes.size()) {
        for (size_t i = 0; i < players.size(); ++i) {
            const Pos cap = capitals[i];
            if (!map.inBounds(cap) || map.unitOn(cap) != Map::kNoUnit) continue;

            const Tribe tribe = Tribe::makeDefault(cfg.tribes[i]);
            const UnitType startType = tribe.getStartUnit().getType();

            const UnitId uid = spawnUnit(startType, static_cast<PlayerId>(i), cap, true);
            if (uid != kNoUnit) {
                (void)CitySystem::addUnitToCity(*this, uid, static_cast<CityId>(i));
            }
        }
    }

    // 4c) initial fog-of-war
    if (capitals.size() == players.size()) {
        for (size_t i = 0; i < players.size(); ++i) {
            VisionSystem::revealArea(*this, static_cast<PlayerId>(i), capitals[i], 2, RevealSource::Initial);
        }
    }

    for (size_t i = 0; i < players.size(); ++i) {
        VisionSystem::revealForPlayerFromUnits(*this, static_cast<PlayerId>(i));
    }

    currentPlayer = 0;
    turnNumber = 0;
    winner = kNoPlayer;

    // 5) start tury gracza 0
    TurnSystem::startTurn(*this);
    updateWinnerByCapitals();
}

bool Game::buyTech(PlayerId pid, TechId tech) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    return TechSystem::buyTech(*this, pid, tech);
}

bool Game::canBuyTech(PlayerId pid, TechId tech) const {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    return TechSystem::canBuyTech(*this, pid, tech);
}

bool Game::endTurn(PlayerId pid) {
    if (isGameOver()) return false;
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;

    TurnSystem::endTurn(*this);

    currentPlayer = static_cast<PlayerId>((currentPlayer + 1) % players.size());
    if (currentPlayer == 0) {
        turnNumber++;
    }

    TurnSystem::startTurn(*this);
    return true;
}

Unit* Game::getUnit(UnitId id) {
    if (id == kNoUnit) return nullptr;
    if (id >= units.size()) return nullptr;
    return &units[id];
}

const Unit* Game::getUnit(UnitId id) const {
    if (id == kNoUnit) return nullptr;
    if (id >= units.size()) return nullptr;
    return &units[id];
}

// Spawns a unit on the map. If `canActImmediately` is false, the unit cannot move/attack until next turn.
bool Game::canSpawnUnit(PlayerId owner, UnitType type, Pos pos, bool canActImmediately) const {
    if (!canActImmediately) {
        if (!isPlayersTurn(owner)) return false;
        if (hasPendingCityUpgrade(owner)) return false;
    }
    if (owner == kNoPlayer) return false;
    if (!PlayerSystem::playerExists(*this, owner)) return false;

    return UnitSpawnSystem::canSpawnUnit(*this, map, type, owner, pos, canActImmediately);
}

// Spawns a unit on the map. If `canActImmediately` is false, the unit cannot move/attack until next turn.
UnitId Game::spawnUnit(UnitType type, PlayerId owner, Pos pos, bool canActImmediately) {
    if (!canSpawnUnit(owner, type, pos, canActImmediately)) return kNoUnit;

    return UnitSpawnSystem::spawnUnit(*this, map, type, owner, pos, canActImmediately);
}

bool Game::moveUnit(PlayerId pid, UnitId unitId, Pos to) {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    return MovementSystem::move(*this, unitId, to);
}

bool Game::canMoveUnit(PlayerId pid, UnitId unitId, Pos to) const {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    const std::vector<Pos> r = MovementSystem::reachable(*this, unitId);
    return std::find(r.begin(), r.end(), to) != r.end();
}

bool Game::handleRuin(PlayerId pid, UnitId unitId, Pos pos) {
    if (!canHandleRuin(pid, unitId, pos)) return false;

    InteractionSystem::handleRuin(*this, unitId, pos);
    return true;
}

bool Game::canHandleRuin(PlayerId pid, UnitId unitId, Pos pos) const {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    if (!map.inBounds(pos)) return false;
    if (UnitSystem::getPos(*this, unitId) != pos) return false;
    if (UnitSystem::movedThisTurn(*this, unitId) || UnitSystem::attackedThisTurn(*this, unitId)) return false;

    const Tile& t = map.at(pos);
    return t.getSettlementType() == SettlementTypeEnum::Ruin;
}

bool Game::handleVillage(PlayerId pid, UnitId unitId, Pos pos) {
    if (!canHandleVillage(pid, unitId, pos)) return false;

    InteractionSystem::handleVillage(*this, unitId, pos);
    return true;
}

bool Game::canHandleVillage(PlayerId pid, UnitId unitId, Pos pos) const {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    if (!map.inBounds(pos)) return false;
    if (UnitSystem::getPos(*this, unitId) != pos) return false;
    if (UnitSystem::movedThisTurn(*this, unitId) || UnitSystem::attackedThisTurn(*this, unitId)) return false;

    const Tile& t = map.at(pos);
    return t.getSettlementType() == SettlementTypeEnum::Village &&
           CitySystem::canFoundCityFromVillage(*this, pid, pos);
}

int Game::getPlayerScore(PlayerId pid) const {
    return ScoreSystem::getScore(*this, pid);
}

bool Game::handleStarfish(PlayerId pid, UnitId unitId, Pos pos) {
    if (!canHandleStarfish(pid, unitId, pos)) return false;

    InteractionSystem::handleStarfish(*this, unitId, pos);
    return true;
}

bool Game::canHandleStarfish(PlayerId pid, UnitId unitId, Pos pos) const {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    if (!PlayerSystem::hasTech(*this, pid, TechId::Navigation)) return false;
    if (!UnitSystem::hasSkill(*this, unitId, UnitSkill::WaterOnly)) return false;
    if (!map.inBounds(pos)) return false;
    if (UnitSystem::getPos(*this, unitId) != pos) return false;
    if (UnitSystem::movedThisTurn(*this, unitId) || UnitSystem::attackedThisTurn(*this, unitId)) return false;

    const Tile& t = map.at(pos);
    return t.getSettlementType() == SettlementTypeEnum::Starfish;
}

bool Game::handleCityCapture(PlayerId pid, UnitId unitId, Pos pos) {
    if (!canHandleCityCapture(pid, unitId, pos)) return false;
    if (!CitySystem::captureCityAt(*this, pid, pos)) return false;
    updateWinnerByCapitals();
    UnitSystem::setMovedThisTurn(*this, unitId, true);
    UnitSystem::setAttackedThisTurn(*this, unitId, true);
    return true;
}

bool Game::canHandleCityCapture(PlayerId pid, UnitId unitId, Pos pos) const {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    if (!map.inBounds(pos)) return false;
    if (UnitSystem::getPos(*this, unitId) != pos) return false;
    if (UnitSystem::movedThisTurn(*this, unitId) || UnitSystem::attackedThisTurn(*this, unitId)) return false;
    return CitySystem::canCaptureCityAt(*this, pid, pos);
}

std::vector<Pos> Game::reachable(PlayerId pid, UnitId unitId) const {
    if (!canControlUnit(*this, pid, unitId, false)) return {};
    return MovementSystem::reachable(*this, unitId);
}

bool Game::attack(PlayerId pid, UnitId attackerId, Pos target) {
    if (!canControlUnit(*this, pid, attackerId, true)) return false;
    return CombatSystem::attack(*this, attackerId, target);
}

bool Game::canAttack(PlayerId pid, UnitId attackerId, Pos target) const {
    if (!canControlUnit(*this, pid, attackerId, true)) return false;
    const std::vector<Pos> a = CombatSystem::attackable(*this, attackerId);
    return std::find(a.begin(), a.end(), target) != a.end();
}


bool Game::heal(PlayerId pid, UnitId healerId) {
    if (!canControlUnit(*this, pid, healerId, true)) return false;
    return CombatSystem::heal(*this, healerId);
}

bool Game::canHeal(PlayerId pid, UnitId healerId) const {
    if (!canControlUnit(*this, pid, healerId, true)) return false;
    if (!UnitSystem::hasSkill(*this, healerId, UnitSkill::Heal)) return false;
    if (UnitSystem::attackedThisTurn(*this, healerId)) return false;
    if (UnitSystem::movedThisTurn(*this, healerId) && !UnitSystem::hasSkill(*this, healerId, UnitSkill::Dash)) return false;

    const Pos center = UnitSystem::getPos(*this, healerId);
    if (!map.inBounds(center)) return false;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;

            const Pos p{center.x + dx, center.y + dy};
            if (!map.inBounds(p)) continue;

            const UnitId uid = map.unitOn(p);
            if (uid == Map::kNoUnit) continue;
            if (!UnitSystem::unitExists(*this, uid)) continue;
            if (UnitSystem::getOwnerId(*this, uid) != pid) continue;
            if (UnitSystem::getHealth(*this, uid) <= 0) continue;
            if (UnitSystem::getHealth(*this, uid) >= UnitSystem::getMaxHealth(*this, uid)) continue;
            return true;
        }
    }
    return false;
}

std::vector<Pos> Game::attackable(PlayerId pid, UnitId attackerId) const {
    if (!canControlUnit(*this, pid, attackerId, false)) return {};
    return CombatSystem::attackable(*this, attackerId);
}

bool Game::buildBuilding(PlayerId builder, Pos pos, BuildingTypeEnum type) {
    if (!isPlayersTurn(builder)) return false;
    if (hasPendingCityUpgrade(builder)) return false;
    if (!map.inBounds(pos)) return false;

    const Tile& t = map.at(pos);
    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return false;
    if (!CitySystem::cityExists(*this, cid)) return false;
    if (static_cast<PlayerId>(CitySystem::getCityOwner(*this, cid)) != builder) return false;

    return BuildingSystem::build(*this, builder, pos, type);
}

bool Game::canBuildBuilding(PlayerId builder, Pos pos, BuildingTypeEnum type) const {
    if (!isPlayersTurn(builder)) return false;
    if (hasPendingCityUpgrade(builder)) return false;
    if (!map.inBounds(pos)) return false;

    const Tile& t = map.at(pos);
    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return false;
    if (!CitySystem::cityExists(*this, cid)) return false;
    if (static_cast<PlayerId>(CitySystem::getCityOwner(*this, cid)) != builder) return false;

    return BuildingSystem::canBuild(*this, builder, pos, type);
}

// ---- Tile actions wrappers ----

bool Game::hunt(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::hunt(*this, pid, pos);
}

bool Game::canHunt(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canHunt(*this, pid, pos);
}

bool Game::organization(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::organization(*this, pid, pos);
}

bool Game::canOrganization(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canOrganization(*this, pid, pos);
}

bool Game::fishing(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::fishing(*this, pid, pos);
}

bool Game::canFishing(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canFishing(*this, pid, pos);
}

bool Game::clearForest(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::clearForest(*this, pid, pos);
}

bool Game::canClearForest(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canClearForest(*this, pid, pos);
}

bool Game::burnForest(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::burnForest(*this, pid, pos);
}

bool Game::canBurnForest(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canBurnForest(*this, pid, pos);
}

bool Game::growForest(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::growForest(*this, pid, pos);
}

bool Game::canGrowForest(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canGrowForest(*this, pid, pos);
}

bool Game::destroyTile(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::destroy(*this, pid, pos);
}

bool Game::canDestroyTile(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canDestroy(*this, pid, pos);
}

bool Game::buildRoad(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::buildRoad(*this, pid, pos);
}

bool Game::canBuildRoad(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canBuildRoad(*this, pid, pos);
}

std::vector<BuildingTypeEnum> Game::getPlayerEarnedMonuments(PlayerId pid) const {
    if (!PlayerSystem::playerExists(*this, pid)) return {};
    return PlayerSystem::getEarnedMonuments(*this, pid);
}

std::vector<BuildingTypeEnum> Game::getPlayerPlacedMonuments(PlayerId pid) const {
    if (!PlayerSystem::playerExists(*this, pid)) return {};
    return PlayerSystem::getPlacedMonuments(*this, pid);
}

std::vector<BuildingTypeEnum> Game::getPlayerOwnedMonuments(PlayerId pid) const {
    if (!PlayerSystem::playerExists(*this, pid)) return {};
    return PlayerSystem::getOwnedMonuments(*this, pid);
}

bool Game::explorer(PlayerId pid, Pos start) {
    if (!canExplorer(pid, start)) return false;
    VisionSystem::doExplorer(*this, pid, start);
    return true;
}

bool Game::canExplorer(PlayerId pid, Pos start) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(start)) return false;

    const Tile& t = map.at(start);
    if (t.getSettlementType() != SettlementTypeEnum::City) return false;

    const CityId cid = static_cast<CityId>(t.getSettlementId());
    if (!CitySystem::cityExists(*this, cid)) return false;
    return static_cast<PlayerId>(CitySystem::getCityOwner(*this, cid)) == pid;
}

bool Game::buildBridge(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::buildBridge(*this, pid, pos);
}

bool Game::canBuildBridge(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    if (!map.inBounds(pos)) return false;
    return TileActionSystem::canBuildBridge(*this, pid, pos);
}

// ---- City actions ----

bool Game::foundCityFromVillage(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    return CitySystem::foundCityFromVillage(*this, pid, pos);
}

bool Game::canFoundCityFromVillage(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    return CitySystem::canFoundCityFromVillage(*this, pid, pos);
}

bool Game::canUpgradeCity(PlayerId pid, CityId cityId) const {
    if (!isPlayersTurn(pid)) return false;
    if (!CitySystem::cityExists(*this, cityId)) return false;

    const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(*this, cityId));
    if (owner != pid) return false;

    const PendingCityUpgrade* p = peekPendingCityUpgrade(pid);
    return p && p->cityId == cityId;
}

CityUpgradeOptions Game::getCityUpgradeOptions(PlayerId pid, CityId cityId) const {
    CityUpgradeOptions out{};

    if (!isPlayersTurn(pid)) return out;
    if (!CitySystem::cityExists(*this, cityId)) return out;

    const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(*this, cityId));
    if (owner != pid) return out;

    const PendingCityUpgrade* p = peekPendingCityUpgrade(pid);
    if (!p || p->cityId != cityId) return out;
    return p->opts;
}

bool Game::upgradeCity(PlayerId pid, CityId cityId, CityUpgradeChoice choice) {
    if (!isPlayersTurn(pid)) return false;
    if (!CitySystem::cityExists(*this, cityId)) return false;

    const PlayerId owner = static_cast<PlayerId>(CitySystem::getCityOwner(*this, cityId));
    if (owner != pid) return false;

    if (pendingCityUpgrades.empty()) return false;
    const PendingCityUpgrade& front = pendingCityUpgrades.front();
    if (front.pid != pid) return false;
    if (front.cityId != cityId) return false;

    if (choice != front.opts.a && choice != front.opts.b) return false;

    if (!CityRewardSystem::tryUpgrade(*this, cityId, choice)) return false;

    pendingCityUpgrades.pop_front();
    return true;
}


bool Game::captureCityAt(PlayerId pid, Pos pos) {
    if (!isPlayersTurn(pid)) return false;
    if (hasPendingCityUpgrade(pid)) return false;
    if (!CitySystem::captureCityAt(*this, pid, pos)) return false;
    updateWinnerByCapitals();
    return true;
}

bool Game::canCaptureCityAt(PlayerId pid, Pos pos) const {
    if (!canCurrentPlayerAct(*this, pid)) return false;
    return CitySystem::canCaptureCityAt(*this, pid, pos);
}

// ---- Raft upgrades (pid-first + legacy delegate) ----

bool Game::canUpgradeRaftToScout(PlayerId pid, UnitId unitId) const {
    if (!canControlUnit(*this, pid, unitId, false)) return false;
    return UnitUpgradeSystem::canUpgradeRaftToScout(*this, unitId);
}


bool Game::canUpgradeRaftToRammer(PlayerId pid, UnitId unitId) const {
    if (!canControlUnit(*this, pid, unitId, false)) return false;
    return UnitUpgradeSystem::canUpgradeRaftToRammer(*this, unitId);
}


bool Game::canUpgradeRaftToBomber(PlayerId pid, UnitId unitId) const {
    if (!canControlUnit(*this, pid, unitId, false)) return false;
    return UnitUpgradeSystem::canUpgradeRaftToBomber(*this, unitId);
}


bool Game::upgradeRaftToScout(PlayerId pid, UnitId unitId) {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    return UnitUpgradeSystem::upgradeRaftToScout(*this, unitId);
}


bool Game::upgradeRaftToRammer(PlayerId pid, UnitId unitId) {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    return UnitUpgradeSystem::upgradeRaftToRammer(*this, unitId);
}


bool Game::upgradeRaftToBomber(PlayerId pid, UnitId unitId) {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    return UnitUpgradeSystem::upgradeRaftToBomber(*this, unitId);
}


// ---- Veteran (pid-first + legacy delegate) ----

bool Game::canUnitBecomeVeteran(PlayerId pid, UnitId unitId) const {
    if (!canControlUnit(*this, pid, unitId, false)) return false;
    return UnitUpgradeSystem::canUnitBecomeVeteran(*this, unitId);
}


bool Game::becomeVeteran(PlayerId pid, UnitId unitId) {
    if (!canControlUnit(*this, pid, unitId, true)) return false;
    return UnitUpgradeSystem::becomeVeteran(*this, unitId);
}

bool Game::canDisbandUnit(PlayerId pid, UnitId unitId) const {
    return DisbandSystem::canDisband(*this, pid, unitId);
}

bool Game::disbandUnit(PlayerId pid, UnitId unitId) {
    return DisbandSystem::disband(*this, pid, unitId);
}


uint16_t Game::getLighthouseDiscoveredByMask(uint8_t lighthouseIdx) const {
    return (lighthouseIdx < lighthouseDiscoveredBy.size()) ? lighthouseDiscoveredBy[lighthouseIdx] : 0;
}

bool Game::hasPlayerDiscoveredLighthouse(uint8_t lighthouseIdx, PlayerId pid) const {
    if (lighthouseIdx >= lighthouseDiscoveredBy.size()) return false;
    if (pid == kNoPlayer) return false;
    const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint16_t>(pid));
    return (lighthouseDiscoveredBy[lighthouseIdx] & bit) != 0;
}

bool Game::markLighthouseDiscovered(uint8_t lighthouseIdx, PlayerId pid) {
    if (lighthouseIdx >= lighthouseDiscoveredBy.size()) return false;
    if (pid == kNoPlayer) return false;

    const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint16_t>(pid));
    uint16_t& mask = lighthouseDiscoveredBy[lighthouseIdx];

    if (mask & bit) return false; // już odkrył wcześniej
    mask |= bit;
    return true;
}

bool Game::hasPendingCityUpgrade(PlayerId pid) const {
    for (const auto& p : pendingCityUpgrades) {
        if (p.pid == pid) return true;
    }
    return false;
}

const Game::PendingCityUpgrade* Game::peekPendingCityUpgrade(PlayerId pid) const {
    for (const auto& p : pendingCityUpgrades) {
        if (p.pid == pid) return &p;
    }
    return nullptr;
}

bool Game::enqueuePendingCityUpgrade(PlayerId pid, CityId cityId) {
    // Prevent duplicates for the same (pid, cityId)
    for (const auto& p : pendingCityUpgrades) {
        if (p.pid == pid && p.cityId == cityId) return false;
    }

    // Level-up is performed in CitySystem/City; we only enqueue the reward choice.
    pendingCityUpgrades.push_back(PendingCityUpgrade{
        pid,
        cityId,
        CityRewardSystem::getUpgradeOptions(*this, cityId),
        turnNumber
    });
    return true;
}

bool Game::resolvePendingCityUpgrade(PlayerId pid, CityUpgradeChoice choice) {
    if (pendingCityUpgrades.empty()) return false;

    const PendingCityUpgrade& front = pendingCityUpgrades.front();
    if (front.pid != pid) return false;

    if (choice != front.opts.a && choice != front.opts.b) return false;

    if (!CityRewardSystem::tryUpgrade(*this, front.cityId, choice)) return false;

    pendingCityUpgrades.pop_front();
    return true;
}

bool Game::hasPlayerSeenCorner(PlayerId pid, MapCorner corner) const {
    if (pid == kNoPlayer) return false;

    const int max = map.getWidth() - 1;

    Pos p{0, 0};
    switch (corner) {
        case MapCorner::TopLeft:     p = Pos{0, 0}; break;
        case MapCorner::BottomLeft:  p = Pos{0, max}; break;
        case MapCorner::TopRight:    p = Pos{max, 0}; break;
        case MapCorner::BottomRight: p = Pos{max, max}; break;
    }

    if (!map.inBounds(p)) return false;

    const Tile& t = map.at(p);
    const uint16_t mask = static_cast<uint16_t>(t.getVisibility());

    const uint32_t idx = static_cast<uint32_t>(pid);
    if (idx >= 16) return false;

    return (mask & (uint16_t(1) << idx)) != 0;
}

void Game::updateWinnerByCapitals() {
    PlayerId capitalOwner = kNoPlayer;
    int capitalsCount = 0;

    for (const City& c : cities) {
        if (!c.getIsCapital()) continue;
        ++capitalsCount;

        const PlayerId owner = static_cast<PlayerId>(c.getOwnerId());
        if (owner == kNoPlayer) {
            winner = kNoPlayer;
            return;
        }

        if (capitalOwner == kNoPlayer) {
            capitalOwner = owner;
        } else if (capitalOwner != owner) {
            winner = kNoPlayer;
            return;
        }
    }

    if (capitalsCount == 0) {
        winner = kNoPlayer;
        return;
    }

    winner = capitalOwner;
}
