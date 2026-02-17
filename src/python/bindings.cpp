#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ai/Action.h"
#include "ai/GameStateAdapter.h"
#include "content/buildings/BuildingDB.h"
#include "content/tribes/Tribe.h"
#include "content/units/UnitFactory.h"
#include "game/Game.h"
#include "systems/GameDataSystem.h"
#include "world/Tile.h"

namespace py = pybind11;

namespace {

static std::string resolveDefaultUnitsJsonPath() {
    // Prefer package data path regardless of current working directory.
    try {
        py::module_ importlibResources = py::module_::import("importlib.resources");
        py::object packageRoot = importlibResources.attr("files")("game_engine");
        py::object unitsPath = packageRoot.attr("joinpath")("data", "Units.json");
        if (py::bool_(unitsPath.attr("is_file")())) {
            return py::cast<std::string>(py::str(unitsPath));
        }
    } catch (const py::error_already_set&) {
        // Fall back to legacy relative path below.
    }

    // Legacy fallback for local C++ app runs from repository root.
    return "data/Units.json";
}

static std::vector<TribeType> parseTribes(const std::vector<int>& tribes) {
    std::vector<TribeType> out;
    out.reserve(tribes.size());
    for (int v : tribes) {
        if (v <= static_cast<int>(TribeType::Unknown) || v > static_cast<int>(TribeType::Cymanti)) {
            throw std::invalid_argument("Invalid tribe id in tribes list.");
        }
        out.push_back(static_cast<TribeType>(v));
    }
    return out;
}

static std::string actionTypeName(Action::Type t) {
    switch (t) {
        case Action::Type::Move: return "move";
        case Action::Type::Attack: return "attack";
        case Action::Type::Heal: return "heal";
        case Action::Type::EndTurn: return "end_turn";
        case Action::Type::BuyTech: return "buy_tech";
        case Action::Type::UpgradeCity: return "upgrade_city";
        case Action::Type::Build: return "build";
        case Action::Type::SpawnUnit: return "spawn_unit";
        case Action::Type::TileAction: return "tile_action";
        case Action::Type::UnitUpgrade: return "unit_upgrade";
    }
    return "unknown";
}

static TechId requiredTechForTileAction(Action::TileActionKind kind) {
    switch (kind) {
        case Action::TileActionKind::Hunt: return TechId::Hunting;
        case Action::TileActionKind::Organization: return TechId::Organization;
        case Action::TileActionKind::Fishing: return TechId::Fishing;
        case Action::TileActionKind::ClearForest: return TechId::Forestry;
        case Action::TileActionKind::BurnForest: return TechId::Construction;
        case Action::TileActionKind::GrowForest: return TechId::Spiritualism;
        case Action::TileActionKind::DestroyTile: return TechId::Chivalry;
        case Action::TileActionKind::BuildRoad: return TechId::Roads;
        case Action::TileActionKind::BuildBridge: return TechId::Roads;
        case Action::TileActionKind::Explorer:
        case Action::TileActionKind::FoundCity:
        case Action::TileActionKind::Ruin:
        case Action::TileActionKind::Starfish:
        case Action::TileActionKind::CaptureCity:
        case Action::TileActionKind::None:
            return TechId::Count;
    }
    return TechId::Count;
}

static int starsCostForTileAction(Action::TileActionKind kind) {
    switch (kind) {
        case Action::TileActionKind::Hunt: return 2;
        case Action::TileActionKind::Organization: return 2;
        case Action::TileActionKind::Fishing: return 2;
        case Action::TileActionKind::ClearForest: return 0;
        case Action::TileActionKind::BurnForest: return 5;
        case Action::TileActionKind::GrowForest: return 5;
        case Action::TileActionKind::DestroyTile: return 0;
        case Action::TileActionKind::BuildRoad: return 3;
        case Action::TileActionKind::BuildBridge: return 5;
        case Action::TileActionKind::Explorer:
        case Action::TileActionKind::FoundCity:
        case Action::TileActionKind::Ruin:
        case Action::TileActionKind::Starfish:
        case Action::TileActionKind::CaptureCity:
        case Action::TileActionKind::None:
            return 0;
    }
    return 0;
}

static int starsCostForUnitUpgrade(Action::UnitUpgradeKind kind) {
    switch (kind) {
        case Action::UnitUpgradeKind::RaftToScout:
            return std::max(0, UnitFactory::getUnitCost(UnitType::Scout));
        case Action::UnitUpgradeKind::RaftToRammer:
            return std::max(0, UnitFactory::getUnitCost(UnitType::Rammer));
        case Action::UnitUpgradeKind::RaftToBomber:
            return std::max(0, UnitFactory::getUnitCost(UnitType::Bomber));
        case Action::UnitUpgradeKind::BecomeVeteran:
        case Action::UnitUpgradeKind::None:
            return 0;
    }
    return 0;
}

static TechId requiredTechForUnitUpgrade(Action::UnitUpgradeKind kind) {
    switch (kind) {
        case Action::UnitUpgradeKind::RaftToScout: return TechId::Sailing;
        case Action::UnitUpgradeKind::RaftToRammer: return TechId::Ramming;
        case Action::UnitUpgradeKind::RaftToBomber: return TechId::Navigation;
        case Action::UnitUpgradeKind::BecomeVeteran:
        case Action::UnitUpgradeKind::None:
            return TechId::Count;
    }
    return TechId::Count;
}

static constexpr int kActionTypeCount = static_cast<int>(Action::Type::UnitUpgrade) + 1;
static constexpr int kTechVocabSize = static_cast<int>(TechId::Count) + 1; // + "none"/Count sentinel
static constexpr int kBuildingVocabSize = static_cast<int>(BuildingTypeEnum::Lighthouse) + 1;
static constexpr int kSpawnTypeVocabSize = static_cast<int>(UnitType::GiantSuper) + 1;
static constexpr int kCityUpgradeVocabSize = static_cast<int>(CityUpgradeChoice::SuperUnit) + 1;
static constexpr int kTileActionVocabSize = static_cast<int>(Action::TileActionKind::CaptureCity) + 1;
static constexpr int kUnitUpgradeVocabSize = static_cast<int>(Action::UnitUpgradeKind::BecomeVeteran) + 1;

struct ActionModelFields {
    int typeId = -1;
    int sourceIndex = -1;
    int targetIndex = -1;
    int city = -1;
    int tech = static_cast<int>(TechId::Count);
    int building = 0;
    int spawnType = 0;
    int upgrade = 0;
    int tileAction = 0;
    int unitUpgrade = 0;
    int costStars = 0;
    int starsBefore = 0;
    int affordable = 1;
};

static ActionModelFields buildActionModelFields(const Game& g, const Action& a) {
    const Map& m = g.getMap();
    ActionModelFields f{};
    f.typeId = static_cast<int>(a.type);

    auto toIndex = [&m](Pos p) -> int {
        if (!m.inBounds(p)) return -1;
        return p.y * m.getWidth() + p.x;
    };

    auto unitPos = [&g](UnitId uid, Pos fallback) -> Pos {
        if (uid == kNoUnit) return fallback;
        if (const Unit* u = g.getUnit(uid)) return u->getPos();
        return fallback;
    };

    Pos sourcePos = a.pos;
    Pos targetPos = a.target;
    bool hasSource = false;
    bool hasTarget = false;

    switch (a.type) {
        case Action::Type::Move:
        case Action::Type::Attack:
            sourcePos = unitPos(a.unit, a.pos);
            hasSource = true;
            hasTarget = true;
            break;
        case Action::Type::Build:
            sourcePos = a.pos;
            targetPos = a.pos;
            hasSource = true;
            hasTarget = true;
            f.tech = static_cast<int>(BuildingDB::getRequiredTech(a.building));
            f.building = static_cast<int>(a.building);
            f.costStars = BuildingDB::getCost(a.building);
            if (m.inBounds(a.pos)) {
                const Tile& t = m.at(a.pos);
                f.city = (t.getTerritoryCityId() == kNoCity) ? -1 : static_cast<int>(t.getTerritoryCityId());
            }
            break;
        case Action::Type::SpawnUnit: {
            sourcePos = a.pos;
            targetPos = a.pos;
            hasSource = true;
            hasTarget = true;
            f.spawnType = static_cast<int>(a.spawnType);
            Unit probe = UnitFactory::create(a.spawnType, a.pid, a.pos);
            f.tech = static_cast<int>(probe.getRequiredTechToSpawn());
            f.costStars = std::max(0, probe.getCost());
            break;
        }
        case Action::Type::TileAction:
            sourcePos = a.pos;
            targetPos = a.pos;
            hasSource = true;
            hasTarget = true;
            f.tileAction = static_cast<int>(a.tileAction);
            f.tech = static_cast<int>(requiredTechForTileAction(a.tileAction));
            f.costStars = starsCostForTileAction(a.tileAction);
            if (a.tileAction == Action::TileActionKind::CaptureCity && m.inBounds(a.pos)) {
                const Tile& t = m.at(a.pos);
                if (t.getSettlementType() == SettlementTypeEnum::City) {
                    f.city = static_cast<int>(t.getSettlementId());
                } else if (t.getTerritoryCityId() != kNoCity) {
                    f.city = static_cast<int>(t.getTerritoryCityId());
                }
            }
            break;
        case Action::Type::UpgradeCity:
            f.upgrade = static_cast<int>(a.upgrade);
            if (a.city != kNoCity) {
                f.city = static_cast<int>(a.city);
                if (const City* c = g.getCity(a.city)) {
                    sourcePos = c->getPos();
                    targetPos = c->getPos();
                    hasSource = true;
                    hasTarget = true;
                }
            }
            break;
        case Action::Type::Heal:
            sourcePos = unitPos(a.unit, a.pos);
            targetPos = sourcePos;
            hasSource = true;
            break;
        case Action::Type::UnitUpgrade:
            sourcePos = unitPos(a.unit, a.pos);
            targetPos = sourcePos;
            hasSource = true;
            f.unitUpgrade = static_cast<int>(a.unitUpgrade);
            f.tech = static_cast<int>(requiredTechForUnitUpgrade(a.unitUpgrade));
            f.costStars = starsCostForUnitUpgrade(a.unitUpgrade);
            break;
        case Action::Type::BuyTech:
            f.tech = static_cast<int>(a.tech);
            if (a.pid != kNoPlayer && static_cast<size_t>(a.pid) < g.getPlayers().size()) {
                const Player& p = g.getPlayer(a.pid);
                const int cityCount = static_cast<int>(p.getCities().size());
                const bool hasLiteracy = p.hasTech(TechId::Philosophy);
                f.costStars = TechDB::calculatePrice(a.tech, cityCount, hasLiteracy);
            }
            break;
        case Action::Type::EndTurn:
            break;
    }

    if (a.city != kNoCity && f.city < 0) {
        f.city = static_cast<int>(a.city);
    }

    f.sourceIndex = hasSource ? toIndex(sourcePos) : -1;
    f.targetIndex = hasTarget ? toIndex(targetPos) : -1;

    if (a.pid != kNoPlayer && static_cast<size_t>(a.pid) < g.getPlayers().size()) {
        f.starsBefore = g.getPlayer(a.pid).getStars();
    }
    f.affordable = (f.costStars <= f.starsBefore) ? 1 : 0;
    return f;
}

static std::pair<int, int> predictAttackDamage(const Game& g, const Action& a) {
    if (a.type != Action::Type::Attack) return {-1, -1};
    if (a.unit == kNoUnit) return {-1, -1};
    if (!g.getMap().inBounds(a.target)) return {-1, -1};

    const Unit* attackerBefore = g.getUnit(a.unit);
    if (!attackerBefore) return {-1, -1};
    const int attackerHpBefore = std::max(0, attackerBefore->getHealth());

    const UnitId defenderId = g.getMap().unitOn(a.target);
    int defenderHpBefore = 0;
    if (defenderId != Map::kNoUnit) {
        if (const Unit* defenderBefore = g.getUnit(defenderId)) {
            defenderHpBefore = std::max(0, defenderBefore->getHealth());
        }
    }

    Game simulated = g;
    if (!simulated.attack(a.pid, a.unit, a.target)) return {-1, -1};

    int attackerHpAfter = 0;
    if (const Unit* attackerAfter = simulated.getUnit(a.unit)) {
        attackerHpAfter = std::max(0, attackerAfter->getHealth());
    }

    int defenderHpAfter = 0;
    if (defenderId != Map::kNoUnit) {
        if (const Unit* defenderAfter = simulated.getUnit(defenderId)) {
            defenderHpAfter = std::max(0, defenderAfter->getHealth());
        }
    }

    const int damageDealt = (defenderId == Map::kNoUnit) ? 0 : std::max(0, defenderHpBefore - defenderHpAfter);
    const int damageReceived = std::max(0, attackerHpBefore - attackerHpAfter);
    return {damageDealt, damageReceived};
}

static std::vector<uint8_t> actionArgMaskForType(Action::Type t) {
    // [source, target, tech, building, spawn_type, city_upgrade, tile_action, unit_upgrade, damage_dealt, damage_received]
    std::vector<uint8_t> mask(10, 0);
    switch (t) {
        case Action::Type::Move:
            mask[0] = 1;
            mask[1] = 1;
            break;
        case Action::Type::Attack:
            mask[0] = 1;
            mask[1] = 1;
            mask[8] = 1;
            mask[9] = 1;
            break;
        case Action::Type::Heal:
            mask[0] = 1;
            break;
        case Action::Type::EndTurn:
            break;
        case Action::Type::BuyTech:
            mask[2] = 1;
            break;
        case Action::Type::UpgradeCity:
            mask[0] = 1;
            mask[5] = 1;
            break;
        case Action::Type::Build:
            mask[0] = 1;
            mask[1] = 1;
            mask[2] = 1;
            mask[3] = 1;
            break;
        case Action::Type::SpawnUnit:
            mask[0] = 1;
            mask[1] = 1;
            mask[2] = 1;
            mask[4] = 1;
            break;
        case Action::Type::TileAction:
            mask[0] = 1;
            mask[1] = 1;
            mask[2] = 1;
            mask[6] = 1;
            break;
        case Action::Type::UnitUpgrade:
            mask[0] = 1;
            mask[2] = 1;
            mask[7] = 1;
            break;
    }
    return mask;
}

class GameEnv {
public:
    GameEnv(int mapSize,
            const std::vector<int>& tribes,
            uint32_t seed,
            const std::string& unitsJsonPath)
        : mapSize_(mapSize)
        , tribesRaw_(tribes)
        , seed_(seed)
        , unitsJsonPath_(unitsJsonPath.empty() ? resolveDefaultUnitsJsonPath() : unitsJsonPath) {
        reset(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    }

    GameEnv(const GameEnv& other)
        : mapSize_(other.mapSize_)
        , tribesRaw_(other.tribesRaw_)
        , seed_(other.seed_)
        , unitsJsonPath_(other.unitsJsonPath_)
        , observationKnownByPlayer_(other.observationKnownByPlayer_)
        , legalIdsCacheValid_(other.legalIdsCacheValid_)
        , legalIdsCachePid_(other.legalIdsCachePid_)
        , legalIdsCache_(other.legalIdsCache_)
        , legalMaskCacheValid_(other.legalMaskCacheValid_)
        , legalMaskCachePid_(other.legalMaskCachePid_)
        , legalMaskCache_(other.legalMaskCache_) {
        if (other.state_) {
            state_ = std::make_unique<GameStateAdapter>(other.state_->getGame());
        }
    }

    GameEnv& operator=(const GameEnv& other) {
        if (this == &other) return *this;
        mapSize_ = other.mapSize_;
        tribesRaw_ = other.tribesRaw_;
        seed_ = other.seed_;
        unitsJsonPath_ = other.unitsJsonPath_;
        observationKnownByPlayer_ = other.observationKnownByPlayer_;
        legalIdsCacheValid_ = other.legalIdsCacheValid_;
        legalIdsCachePid_ = other.legalIdsCachePid_;
        legalIdsCache_ = other.legalIdsCache_;
        legalMaskCacheValid_ = other.legalMaskCacheValid_;
        legalMaskCachePid_ = other.legalMaskCachePid_;
        legalMaskCache_ = other.legalMaskCache_;
        if (other.state_) {
            state_ = std::make_unique<GameStateAdapter>(other.state_->getGame());
        } else {
            state_.reset();
        }
        return *this;
    }

    GameEnv(GameEnv&&) noexcept = default;
    GameEnv& operator=(GameEnv&&) noexcept = default;

    py::dict reset(std::optional<int> mapSize,
                   std::optional<std::vector<int>> tribes,
                   std::optional<uint32_t> seed,
                   std::optional<std::string> unitsJsonPath) {
        if (mapSize) mapSize_ = *mapSize;
        if (tribes) tribesRaw_ = *tribes;
        if (seed) seed_ = *seed;
        if (unitsJsonPath) unitsJsonPath_ = *unitsJsonPath;

        if (unitsJsonPath_.empty()) unitsJsonPath_ = resolveDefaultUnitsJsonPath();
        GameDataSystem::loadUnits(unitsJsonPath_);

        Game game;
        Game::NewGameConfig cfg;
        cfg.mapSize = mapSize_;
        cfg.seed = seed_;
        cfg.tribes = parseTribes(tribesRaw_.empty() ? std::vector<int>{3, 2} : tribesRaw_);
        game.newGame(cfg);

        state_ = std::make_unique<GameStateAdapter>(std::move(game));
        initializeObservationKnowledgeFromCurrentVisibility();
        invalidateCaches();
        return observation(std::nullopt, false, -1);
    }

    py::dict step(size_t actionId, std::optional<int> rewardPlayer) {
        ensureState();
        const PlayerId actor = state_->currentPlayer();
        const Game& gBefore = state_->getGame();
        const int starsBefore = gBefore.getPlayer(actor).getStars();
        const auto decoded = state_->decodeActionId(actor, actionId);
        if (!decoded) {
            py::dict out;
            out["ok"] = false;
            out["done"] = state_->isTerminal();
            out["reward"] = 0.0f;
            out["selected_action_id"] = static_cast<int>(actionId);
            out["stars_before"] = starsBefore;
            out["stars_after"] = starsBefore;
            out["delta_stars"] = 0;
            out["action_cost_stars"] = -1;
            out["action_affordable"] = 0;
            out["observation"] = observation(std::nullopt, false, -1);
            return out;
        }
        const ActionModelFields actionFeatures = buildActionModelFields(gBefore, *decoded);

        state_->apply(*decoded);
        revealObservedTilesForAllPlayers();
        invalidateCaches();

        const bool done = state_->isTerminal();
        const Game& g = state_->getGame();
        const int starsAfter = g.getPlayer(actor).getStars();
        int rp = rewardPlayer.value_or(static_cast<int>(actor));
        float reward = 0.0f;
        if (done && rp >= 0 && rp < static_cast<int>(g.getPlayers().size()) && g.isGameOver()) {
            reward = (g.getWinner() == static_cast<PlayerId>(rp)) ? 1.0f : -1.0f;
        }

        py::dict out;
        out["ok"] = true;
        out["done"] = done;
        out["reward"] = reward;
        out["winner"] = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        out["current_player"] = static_cast<int>(state_->currentPlayer());
        out["selected_action_id"] = static_cast<int>(actionId);
        out["stars_before"] = starsBefore;
        out["stars_after"] = starsAfter;
        out["delta_stars"] = starsAfter - starsBefore;
        out["action_cost_stars"] = actionFeatures.costStars;
        out["action_affordable"] = actionFeatures.affordable;
        out["observation"] = observation(std::nullopt, false, -1);
        return out;
    }

    py::tuple stepFast(size_t actionId, std::optional<int> rewardPlayer) {
        ensureState();
        const PlayerId actor = state_->currentPlayer();
        const auto decoded = state_->decodeActionId(actor, actionId);
        if (!decoded) {
            return py::make_tuple(false, state_->isTerminal(), 0.0f, -1, static_cast<int>(state_->currentPlayer()));
        }

        state_->apply(*decoded);
        revealObservedTilesForAllPlayers();
        invalidateCaches();

        const bool done = state_->isTerminal();
        const Game& g = state_->getGame();
        int rp = rewardPlayer.value_or(static_cast<int>(actor));
        float reward = 0.0f;
        if (done && rp >= 0 && rp < static_cast<int>(g.getPlayers().size()) && g.isGameOver()) {
            reward = (g.getWinner() == static_cast<PlayerId>(rp)) ? 1.0f : -1.0f;
        }
        const int winner = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        return py::make_tuple(true, done, reward, winner, static_cast<int>(state_->currentPlayer()));
    }

    py::tuple stepFastNoReveal(size_t actionId, std::optional<int> rewardPlayer) {
        ensureState();
        const PlayerId actor = state_->currentPlayer();
        const auto decoded = state_->decodeActionId(actor, actionId);
        if (!decoded) {
            return py::make_tuple(false, state_->isTerminal(), 0.0f, -1, static_cast<int>(state_->currentPlayer()));
        }

        state_->apply(*decoded);
        // Keep mechanics/FOW updates from the core engine, but do not expose newly visible
        // tile content to the acting player's visible_only observation layers.
        revealObservedTilesForAllPlayersExcept(actor);
        invalidateCaches();

        const bool done = state_->isTerminal();
        const Game& g = state_->getGame();
        int rp = rewardPlayer.value_or(static_cast<int>(actor));
        float reward = 0.0f;
        if (done && rp >= 0 && rp < static_cast<int>(g.getPlayers().size()) && g.isGameOver()) {
            reward = (g.getWinner() == static_cast<PlayerId>(rp)) ? 1.0f : -1.0f;
        }
        const int winner = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        return py::make_tuple(true, done, reward, winner, static_cast<int>(state_->currentPlayer()));
    }

    py::dict observation(std::optional<int> playerId,
                         bool visibleOnly,
                         int hiddenValue) const {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int perspective = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));

        py::dict obs;
        obs["turn"] = g.getTurnNumber();
        obs["current_player"] = static_cast<int>(g.getCurrentPlayerId());
        obs["game_over"] = g.isGameOver();
        obs["winner"] = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        std::vector<int> playerStars;
        std::vector<int> playerCityCounts;
        std::vector<int> playerUnitCounts;
        playerStars.reserve(g.getPlayers().size());
        playerCityCounts.reserve(g.getPlayers().size());
        playerUnitCounts.reserve(g.getPlayers().size());
        for (const Player& p : g.getPlayers()) {
            playerStars.push_back(p.getStars());
            playerCityCounts.push_back(static_cast<int>(p.getCities().size()));
            playerUnitCounts.push_back(static_cast<int>(p.getUnits().size()));
        }
        int perspectiveStars = -1;
        if (perspective >= 0 && static_cast<size_t>(perspective) < g.getPlayers().size()) {
            perspectiveStars = g.getPlayer(static_cast<PlayerId>(perspective)).getStars();
        }
        obs["player_stars"] = perspectiveStars;
        obs["stars"] = playerStars;
        obs["player_stars_by_player"] = std::move(playerStars);
        obs["player_city_counts"] = std::move(playerCityCounts);
        obs["player_unit_counts"] = std::move(playerUnitCounts);
        const Game::PendingCityUpgrade* pending = g.peekPendingCityUpgrade(g.getCurrentPlayerId());
        obs["pending_city_upgrade"] = (pending != nullptr);
        if (pending) {
        obs["pending_city_id"] = static_cast<int>(pending->cityId);
            obs["pending_upgrade_a"] = static_cast<int>(pending->opts.a);
            obs["pending_upgrade_b"] = static_cast<int>(pending->opts.b);
        } else {
            obs["pending_city_id"] = -1;
            obs["pending_upgrade_a"] = static_cast<int>(CityUpgradeChoice::None);
            obs["pending_upgrade_b"] = static_cast<int>(CityUpgradeChoice::None);
        }
        obs["visible_only"] = visibleOnly;
        obs["hidden_value"] = hiddenValue;
        if (!visibleOnly) {
            obs["lighthouse_discovered_by_masks"] = lighthouseDiscoveredByMasks();
            obs["lighthouse_discovered_by_players"] = lighthouseDiscoveredByPlayers();
        } else {
            std::vector<int> knownMasks(4, 0);
            std::vector<std::vector<int>> knownPlayers(4);
            const auto allMasks = lighthouseDiscoveredByMasks();
            const auto allPlayers = lighthouseDiscoveredByPlayers();
            if (perspective >= 0 && perspective < 16) {
                for (uint8_t i = 0; i < 4; ++i) {
                    if (!g.hasPlayerDiscoveredLighthouse(i, static_cast<PlayerId>(perspective))) continue;
                    knownMasks[i] = allMasks[i];
                    knownPlayers[i] = allPlayers[i];
                }
            }
            obs["lighthouse_discovered_by_masks"] = std::move(knownMasks);
            obs["lighthouse_discovered_by_players"] = std::move(knownPlayers);
        }
        obs["map_size"] = w;
        obs["actions"] = legalParamActions();
        obs["tokenized_map"] = tokenizedMap(playerId, visibleOnly, hiddenValue);
        obs["techs"] = getTechs(playerId);
        obs["seen_lighthouses"] = seenLighthouses(playerId);
        return obs;
    }

    std::vector<std::vector<int>> tokenizedMap(std::optional<int> playerId = std::nullopt,
                                               bool visibleOnly = false,
                                               int hiddenValue = -1) const {
        ensureState();
        if (visibleOnly) {
            ensureObservationKnowledgeLayout();
        }
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int perspective = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));
        const std::vector<uint8_t>* knownObs = nullptr;
        if (visibleOnly && perspective >= 0) {
            const size_t perspectiveIdx = static_cast<size_t>(perspective);
            if (perspectiveIdx < observationKnownByPlayer_.size()) {
                knownObs = &observationKnownByPlayer_[perspectiveIdx];
            }
        }
        bool hasClimbing = false;
        bool hasOrganization = false;
        bool hasFishing = false;
        if (perspective >= 0 && perspective < static_cast<int>(g.getPlayers().size())) {
            const Player& p = g.getPlayer(static_cast<PlayerId>(perspective));
            hasClimbing = p.hasTech(TechId::Climbing);
            hasOrganization = p.hasTech(TechId::Organization);
            hasFishing = p.hasTech(TechId::Fishing);
        }

        auto hasActivatedHide = [](const Unit* u) -> bool {
            if (!u) return false;
            if (!u->hasSkill(UnitSkill::Hide)) return false;
            const Pos d = u->getLastMoveDir();
            return !(d.x == 0 && d.y == 0);
        };

        auto isEnemyHiddenUnitForPerspective = [&](const Unit* u) -> bool {
            if (!u) return false;
            if (perspective < 0 || perspective >= 16) return false;
            const bool isEnemy = static_cast<int>(u->getOwnerId()) != perspective;
            return isEnemy && hasActivatedHide(u);
        };

        auto hasAdjacentEnemyHiddenUnit = [&](Pos center) -> bool {
            if (perspective < 0 || perspective >= 16) return false;
            static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            for (int i = 0; i < 8; ++i) {
                const Pos np{center.x + kDx[i], center.y + kDy[i]};
                if (!m.inBounds(np)) continue;
                const UnitId nUid = m.unitOn(np);
                if (nUid == Map::kNoUnit) continue;
                const Unit* nu = g.getUnit(nUid);
                if (isEnemyHiddenUnitForPerspective(nu)) return true;
            }
            return false;
        };

        const size_t tileCount = static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h));
        std::vector<std::vector<int>> out;
        out.reserve(tileCount);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const Tile& t = m.at(p);
                const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());
                const int visibility = (perspective >= 0 && perspective < 16 &&
                                        ((visMask & (uint16_t(1) << perspective)) != 0))
                                           ? 1
                                           : 0;
                const int idx = y * w + x;
                bool knownToObservation = true;
                if (visibleOnly) {
                    knownToObservation = (visibility == 1);
                    if (knownToObservation && knownObs && static_cast<size_t>(idx) < knownObs->size()) {
                        knownToObservation = ((*knownObs)[static_cast<size_t>(idx)] != 0);
                    }
                }

                int unitHp = -1;
                int unitOwner = -1;
                int unitId = static_cast<int>(Map::kNoUnit);
                int isCloakAround = 0;
                int capitalLayer = -1;
                int ownUnitKills = -1;
                int resourceToken = static_cast<int>(t.getResource());
                int settlementTypeToken = static_cast<int>(t.getSettlementType());
                int settlementIdToken =
                    (static_cast<int>(t.getSettlementId()) == static_cast<int>(kNoSettlement))
                        ? -1
                        : static_cast<int>(t.getSettlementId());

                if (!hasClimbing && resourceToken == static_cast<int>(ResourcesEnum::Metal)) {
                    resourceToken = static_cast<int>(ResourcesEnum::None);
                }
                if (!hasOrganization && resourceToken == static_cast<int>(ResourcesEnum::Crops)) {
                    resourceToken = static_cast<int>(ResourcesEnum::None);
                }
                if (!hasFishing && settlementTypeToken == static_cast<int>(SettlementTypeEnum::Starfish)) {
                    settlementTypeToken = static_cast<int>(SettlementTypeEnum::None);
                    settlementIdToken = -1;
                }
                if (t.getSettlementType() == SettlementTypeEnum::City) {
                    capitalLayer = 0;
                    const CityId cityId = static_cast<CityId>(t.getSettlementId());
                    if (cityId != kNoCity) {
                        const City* city = g.getCity(cityId);
                        if (city) {
                            const PlayerId owner = city->getOwnerId();
                            if (owner != kNoPlayer &&
                                static_cast<size_t>(owner) < g.getPlayers().size() &&
                                g.getPlayer(owner).getCapitalId() == city->getCityId()) {
                                capitalLayer = 1;
                            }
                        }
                    }
                }

                const UnitId uid = m.unitOn(p);
                if (uid != Map::kNoUnit) {
                    const Unit* u = g.getUnit(uid);
                    if (u) {
                        const bool hiddenEnemy = isEnemyHiddenUnitForPerspective(u);
                        if (!hiddenEnemy) {
                            unitId = static_cast<int>(uid);
                            unitHp = u->getHealth();
                            unitOwner = static_cast<int>(u->getOwnerId());
                            if (perspective >= 0 &&
                                static_cast<int>(u->getOwnerId()) == perspective) {
                                ownUnitKills = u->getKillCounter();
                            }
                        }
                        if (perspective >= 0 && perspective < 16 &&
                            static_cast<int>(u->getOwnerId()) == perspective &&
                            hasAdjacentEnemyHiddenUnit(p)) {
                            isCloakAround = 1;
                        }
                    }
                }

                std::vector<int> tileToken{
                    visibility,
                    isCloakAround,
                    unitHp,
                    unitOwner,
                    (unitId == static_cast<int>(Map::kNoUnit)) ? -1 : unitId,
                    ownUnitKills,
                    (static_cast<int>(t.getTerritoryCityId()) == static_cast<int>(kNoCity)) ? -1 : static_cast<int>(t.getTerritoryCityId()),
                    static_cast<int>(t.getRoadBridge()),
                    static_cast<int>(t.getBuildingType()),
                    capitalLayer,
                    settlementTypeToken,
                    settlementIdToken,
                    resourceToken,
                    static_cast<int>(t.getBaseTerrain()),
                    static_cast<int>(t.getTribe()),
                };
                if (visibleOnly && !knownToObservation) {
                    for (size_t i = 1; i < tileToken.size(); ++i) {
                        tileToken[i] = hiddenValue;
                    }
                }
                out.push_back(std::move(tileToken));
            }
        }

        return out;
    }

    std::vector<int> lighthouseDiscoveredByMasks() const {
        ensureState();
        const Game& g = state_->getGame();
        std::vector<int> out(4, 0);
        for (uint8_t i = 0; i < 4; ++i) {
            out[i] = static_cast<int>(g.getLighthouseDiscoveredByMask(i));
        }
        return out;
    }

    std::vector<std::vector<int>> lighthouseDiscoveredByPlayers() const {
        ensureState();
        const auto masks = lighthouseDiscoveredByMasks();
        std::vector<std::vector<int>> out(4);
        for (size_t i = 0; i < masks.size(); ++i) {
            for (int pid = 0; pid < 16; ++pid) {
                if ((masks[i] & (1 << pid)) != 0) {
                    out[i].push_back(pid);
                }
            }
        }
        return out;
    }

    py::dict lighthouseVisibility(std::optional<int> playerId) const {
        ensureState();
        const Game& g = state_->getGame();
        const int perspective = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));

        std::vector<int> knownMasks(4, 0);
        std::vector<std::vector<int>> knownPlayers(4);
        std::vector<int> discoveredByPlayer(4, 0);

        const auto allMasks = lighthouseDiscoveredByMasks();
        const auto allPlayers = lighthouseDiscoveredByPlayers();

        const bool perspectiveValid = perspective >= 0 && perspective < 16;
        for (uint8_t i = 0; i < 4; ++i) {
            if (!perspectiveValid) continue;
            const bool discovered = g.hasPlayerDiscoveredLighthouse(i, static_cast<PlayerId>(perspective));
            discoveredByPlayer[i] = discovered ? 1 : 0;
            if (discovered) {
                knownMasks[i] = allMasks[i];
                knownPlayers[i] = allPlayers[i];
            }
        }

        py::dict out;
        out["player_id"] = perspective;
        out["discovered_by_player"] = std::move(discoveredByPlayer);
        out["known_masks"] = std::move(knownMasks);
        out["known_players"] = std::move(knownPlayers);
        out["all_masks"] = std::move(allMasks);
        out["all_players"] = std::move(allPlayers);
        return out;
    }

    std::vector<uint8_t> legalActionMask() const {
        ensureState();
        ensureLegalMaskCache();
        return legalMaskCache_;
    }

    std::vector<size_t> legalActionIds() const {
        ensureState();
        ensureLegalIdsCache();
        return legalIdsCache_;
    }

    std::vector<size_t> legalActionIdsFast() const {
        ensureState();
        ensureLegalIdsCache();
        return legalIdsCache_;
    }

    py::dict actionParamSpec() const {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        py::dict spec;
        spec["type_vocab_size"] = kActionTypeCount;
        spec["tile_vocab_size"] = m.getWidth() * m.getHeight();
        spec["tech_vocab_size"] = kTechVocabSize;
        spec["building_vocab_size"] = kBuildingVocabSize;
        spec["spawn_type_vocab_size"] = kSpawnTypeVocabSize;
        spec["city_upgrade_vocab_size"] = kCityUpgradeVocabSize;
        spec["tile_action_vocab_size"] = kTileActionVocabSize;
        spec["unit_upgrade_vocab_size"] = kUnitUpgradeVocabSize;
        spec["tech_none_id"] = static_cast<int>(TechId::Count);
        spec["source_none_id"] = -1;
        spec["target_none_id"] = -1;
        spec["city_none_id"] = -1;
        spec["map_width"] = m.getWidth();
        spec["map_height"] = m.getHeight();
        spec["arg_order"] = std::vector<std::string>{
            "source_index", "target_index", "tech", "building", "spawn_type", "upgrade", "tile_action", "unit_upgrade",
            "damage_dealt", "damage_received"};
        return spec;
    }

    std::vector<py::dict> legalParamActions() const {
        ensureState();
        ensureLegalIdsCache();
        const Game& g = state_->getGame();
        std::vector<py::dict> out;
        out.reserve(legalIdsCache_.size());
        const int currentStars = g.getPlayer(g.getCurrentPlayerId()).getStars();
        for (size_t aid : legalIdsCache_) {
            const auto a = state_->decodeActionId(state_->currentPlayer(), aid);
            if (!a) continue;
            const ActionModelFields f = buildActionModelFields(g, *a);
            py::dict d;
            d["action_id"] = aid;
            d["type"] = actionTypeName(a->type);
            d["type_id"] = f.typeId;
            d["source_index"] = f.sourceIndex;
            d["target_index"] = f.targetIndex;
            d["city"] = f.city;
            d["tech"] = f.tech;
            d["building"] = f.building;
            d["spawn_type"] = f.spawnType;
            d["upgrade"] = f.upgrade;
            d["tile_action"] = f.tileAction;
            d["unit_upgrade"] = f.unitUpgrade;
            d["cost_stars"] = f.costStars;
            d["stars_before"] = currentStars;
            d["affordable"] = (f.costStars <= currentStars) ? 1 : 0;
            const auto [damageDealt, damageReceived] = predictAttackDamage(g, *a);
            d["damage_dealt"] = damageDealt;
            d["damage_received"] = damageReceived;
            d["arg_mask"] = actionArgMaskForType(a->type);
            out.push_back(std::move(d));
        }
        return out;
    }

    py::dict legalParamMasks() const {
        ensureState();
        ensureLegalIdsCache();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const int tileCount = m.getWidth() * m.getHeight();

        std::vector<uint8_t> typeMask(kActionTypeCount, 0);
        std::vector<uint8_t> sourceMask(std::max(0, tileCount), 0);
        std::vector<uint8_t> targetMask(std::max(0, tileCount), 0);
        std::vector<uint8_t> techMask(kTechVocabSize, 0);
        std::vector<uint8_t> buildingMask(kBuildingVocabSize, 0);
        std::vector<uint8_t> spawnTypeMask(kSpawnTypeVocabSize, 0);
        std::vector<uint8_t> upgradeMask(kCityUpgradeVocabSize, 0);
        std::vector<uint8_t> tileActionMask(kTileActionVocabSize, 0);
        std::vector<uint8_t> unitUpgradeMask(kUnitUpgradeVocabSize, 0);

        std::vector<std::vector<uint8_t>> sourceMaskByType(kActionTypeCount, std::vector<uint8_t>(std::max(0, tileCount), 0));
        std::vector<std::vector<uint8_t>> targetMaskByType(kActionTypeCount, std::vector<uint8_t>(std::max(0, tileCount), 0));
        std::vector<std::vector<uint8_t>> techMaskByType(kActionTypeCount, std::vector<uint8_t>(kTechVocabSize, 0));
        std::vector<std::vector<uint8_t>> buildingMaskByType(kActionTypeCount, std::vector<uint8_t>(kBuildingVocabSize, 0));
        std::vector<std::vector<uint8_t>> spawnTypeMaskByType(kActionTypeCount, std::vector<uint8_t>(kSpawnTypeVocabSize, 0));
        std::vector<std::vector<uint8_t>> upgradeMaskByType(kActionTypeCount, std::vector<uint8_t>(kCityUpgradeVocabSize, 0));
        std::vector<std::vector<uint8_t>> tileActionMaskByType(kActionTypeCount, std::vector<uint8_t>(kTileActionVocabSize, 0));
        std::vector<std::vector<uint8_t>> unitUpgradeMaskByType(kActionTypeCount, std::vector<uint8_t>(kUnitUpgradeVocabSize, 0));
        std::unordered_map<long long, std::vector<uint8_t>> targetMaskByTypeSource;

        const int currentStars = g.getPlayer(g.getCurrentPlayerId()).getStars();
        std::vector<int> actionCostById(state_->actionSpace().size(), -1);
        std::vector<uint8_t> affordableMask(state_->actionSpace().size(), 0);

        for (size_t aid : legalIdsCache_) {
            const auto a = state_->decodeActionId(state_->currentPlayer(), aid);
            if (!a) continue;
            const ActionModelFields f = buildActionModelFields(g, *a);
            if (f.typeId < 0 || f.typeId >= kActionTypeCount) continue;

            typeMask[static_cast<size_t>(f.typeId)] = 1;
            if (f.sourceIndex >= 0 && f.sourceIndex < tileCount) {
                sourceMask[static_cast<size_t>(f.sourceIndex)] = 1;
                sourceMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.sourceIndex)] = 1;
            }
            if (f.targetIndex >= 0 && f.targetIndex < tileCount) {
                targetMask[static_cast<size_t>(f.targetIndex)] = 1;
                targetMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.targetIndex)] = 1;
                const long long key = (static_cast<long long>(f.typeId) << 32) | static_cast<unsigned int>(f.sourceIndex + 1);
                auto& dstMask = targetMaskByTypeSource[key];
                if (dstMask.empty()) dstMask.assign(static_cast<size_t>(tileCount), 0);
                dstMask[static_cast<size_t>(f.targetIndex)] = 1;
            }
            if (f.tech >= 0 && f.tech < kTechVocabSize) {
                techMask[static_cast<size_t>(f.tech)] = 1;
                techMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.tech)] = 1;
            }
            if (f.building >= 0 && f.building < kBuildingVocabSize) {
                buildingMask[static_cast<size_t>(f.building)] = 1;
                buildingMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.building)] = 1;
            }
            if (f.spawnType >= 0 && f.spawnType < kSpawnTypeVocabSize) {
                spawnTypeMask[static_cast<size_t>(f.spawnType)] = 1;
                spawnTypeMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.spawnType)] = 1;
            }
            if (f.upgrade >= 0 && f.upgrade < kCityUpgradeVocabSize) {
                upgradeMask[static_cast<size_t>(f.upgrade)] = 1;
                upgradeMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.upgrade)] = 1;
            }
            if (f.tileAction >= 0 && f.tileAction < kTileActionVocabSize) {
                tileActionMask[static_cast<size_t>(f.tileAction)] = 1;
                tileActionMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.tileAction)] = 1;
            }
            if (f.unitUpgrade >= 0 && f.unitUpgrade < kUnitUpgradeVocabSize) {
                unitUpgradeMask[static_cast<size_t>(f.unitUpgrade)] = 1;
                unitUpgradeMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.unitUpgrade)] = 1;
            }
            if (aid < actionCostById.size()) {
                actionCostById[aid] = f.costStars;
                affordableMask[aid] = (f.costStars <= currentStars) ? 1 : 0;
            }
        }

        py::dict sourceByType;
        py::dict targetByType;
        py::dict techByType;
        py::dict buildingByType;
        py::dict spawnByType;
        py::dict upgradeByType;
        py::dict tileActionByType;
        py::dict unitUpgradeByType;
        for (int t = 0; t < kActionTypeCount; ++t) {
            if (!typeMask[static_cast<size_t>(t)]) continue;
            sourceByType[py::int_(t)] = sourceMaskByType[static_cast<size_t>(t)];
            targetByType[py::int_(t)] = targetMaskByType[static_cast<size_t>(t)];
            techByType[py::int_(t)] = techMaskByType[static_cast<size_t>(t)];
            buildingByType[py::int_(t)] = buildingMaskByType[static_cast<size_t>(t)];
            spawnByType[py::int_(t)] = spawnTypeMaskByType[static_cast<size_t>(t)];
            upgradeByType[py::int_(t)] = upgradeMaskByType[static_cast<size_t>(t)];
            tileActionByType[py::int_(t)] = tileActionMaskByType[static_cast<size_t>(t)];
            unitUpgradeByType[py::int_(t)] = unitUpgradeMaskByType[static_cast<size_t>(t)];
        }

        py::dict targetByTypeSource;
        for (const auto& [key, mask] : targetMaskByTypeSource) {
            const int typeId = static_cast<int>(key >> 32);
            const int sourceIdx = static_cast<int>(key & 0xFFFFFFFFu) - 1;
            targetByTypeSource[py::str(std::to_string(typeId) + ":" + std::to_string(sourceIdx))] = mask;
        }

        py::dict out;
        out["type_mask"] = std::move(typeMask);
        out["source_mask"] = std::move(sourceMask);
        out["target_mask"] = std::move(targetMask);
        out["tech_mask"] = std::move(techMask);
        out["building_mask"] = std::move(buildingMask);
        out["spawn_type_mask"] = std::move(spawnTypeMask);
        out["upgrade_mask"] = std::move(upgradeMask);
        out["tile_action_mask"] = std::move(tileActionMask);
        out["unit_upgrade_mask"] = std::move(unitUpgradeMask);
        out["source_mask_by_type"] = std::move(sourceByType);
        out["target_mask_by_type"] = std::move(targetByType);
        out["target_mask_by_type_source"] = std::move(targetByTypeSource);
        out["tech_mask_by_type"] = std::move(techByType);
        out["building_mask_by_type"] = std::move(buildingByType);
        out["spawn_type_mask_by_type"] = std::move(spawnByType);
        out["upgrade_mask_by_type"] = std::move(upgradeByType);
        out["tile_action_mask_by_type"] = std::move(tileActionByType);
        out["unit_upgrade_mask_by_type"] = std::move(unitUpgradeByType);
        out["legal_count"] = static_cast<int>(legalIdsCache_.size());
        out["current_player"] = static_cast<int>(g.getCurrentPlayerId());
        out["turn"] = g.getTurnNumber();
        out["stars_before"] = currentStars;
        out["pending_city_upgrade"] = g.hasPendingCityUpgrade(g.getCurrentPlayerId());
        out["action_cost_by_id"] = std::move(actionCostById);
        out["action_affordable_mask"] = std::move(affordableMask);
        return out;
    }

    py::dict stepParam(const py::dict& param, std::optional<int> rewardPlayer) {
        ensureState();
        ensureLegalIdsCache();

        if (param.contains("action_id")) {
            const size_t actionId = py::cast<size_t>(param["action_id"]);
            py::dict out = step(actionId, rewardPlayer);
            out["selected_action_id"] = static_cast<int>(actionId);
            return out;
        }
        if (!param.contains("type_id")) {
            py::dict out;
            out["ok"] = false;
            out["done"] = state_->isTerminal();
            out["reward"] = 0.0f;
            out["selected_action_id"] = -1;
            out["observation"] = observation(std::nullopt, false, -1);
            out["error"] = "Missing required field: type_id";
            return out;
        }

        const int wantedType = py::cast<int>(param["type_id"]);
        auto has = [&param](const char* key) { return param.contains(key); };
        auto getInt = [&param](const char* key) { return py::cast<int>(param[key]); };

        int selected = -1;
        const Game& g = state_->getGame();
        for (size_t aid : legalIdsCache_) {
            const auto a = state_->decodeActionId(state_->currentPlayer(), aid);
            if (!a) continue;
            const ActionModelFields f = buildActionModelFields(g, *a);
            if (f.typeId != wantedType) continue;
            if (has("source_index") && f.sourceIndex != getInt("source_index")) continue;
            if (has("target_index") && f.targetIndex != getInt("target_index")) continue;
            if (has("tech") && f.tech != getInt("tech")) continue;
            if (has("building") && f.building != getInt("building")) continue;
            if (has("spawn_type") && f.spawnType != getInt("spawn_type")) continue;
            if (has("upgrade") && f.upgrade != getInt("upgrade")) continue;
            if (has("tile_action") && f.tileAction != getInt("tile_action")) continue;
            if (has("unit_upgrade") && f.unitUpgrade != getInt("unit_upgrade")) continue;
            if (has("city") && f.city != getInt("city")) continue;
            selected = static_cast<int>(aid);
            break;
        }

        if (selected < 0) {
            py::dict out;
            out["ok"] = false;
            out["done"] = state_->isTerminal();
            out["reward"] = 0.0f;
            out["selected_action_id"] = -1;
            out["observation"] = observation(std::nullopt, false, -1);
            out["error"] = "No legal action matches provided param tuple.";
            return out;
        }

        py::dict out = step(static_cast<size_t>(selected), rewardPlayer);
        out["selected_action_id"] = selected;
        return out;
    }

    py::dict stepParamVec(const std::vector<int>& vec, std::optional<int> rewardPlayer) {
        if (vec.empty()) {
            py::dict out;
            out["ok"] = false;
            out["done"] = isDone();
            out["reward"] = 0.0f;
            out["selected_action_id"] = -1;
            out["observation"] = observation(std::nullopt, false, -1);
            out["error"] = "Vector must contain at least type_id.";
            return out;
        }

        // [type_id, source_index, target_index, tech, building, spawn_type, upgrade, tile_action, unit_upgrade, city]
        py::dict param;
        param["type_id"] = vec[0];
        const std::vector<std::string> keys = {
            "source_index", "target_index", "tech", "building", "spawn_type",
            "upgrade", "tile_action", "unit_upgrade", "city"};
        for (size_t i = 1; i < vec.size() && i <= keys.size(); ++i) {
            if (vec[i] < 0) continue;
            param[py::str(keys[i - 1])] = vec[i];
        }
        return stepParam(param, rewardPlayer);
    }

    py::dict decodeAction(size_t actionId) const {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const auto a = state_->decodeActionId(state_->currentPlayer(), actionId);
        if (!a) return py::dict();

        const ActionModelFields f = buildActionModelFields(g, *a);
        Pos resolvedTarget{-1, -1};
        if (f.targetIndex >= 0 && m.getWidth() > 0) {
            resolvedTarget = Pos{f.targetIndex % m.getWidth(), f.targetIndex / m.getWidth()};
        }
        Pos resolvedPos{-1, -1};
        if (f.sourceIndex >= 0 && m.getWidth() > 0) {
            resolvedPos = Pos{f.sourceIndex % m.getWidth(), f.sourceIndex / m.getWidth()};
        }

        py::dict out;
        out["action_id"] = actionId;
        out["type"] = actionTypeName(a->type);
        out["type_id"] = f.typeId;
        out["pid"] = static_cast<int>(a->pid);
        out["unit"] = (a->unit == kNoUnit) ? -1 : static_cast<int>(a->unit);
        out["city"] = f.city;
        out["pos"] = py::make_tuple(resolvedPos.x, resolvedPos.y);
        out["target"] = py::make_tuple(resolvedTarget.x, resolvedTarget.y);
        out["target_x"] = resolvedTarget.x;
        out["target_y"] = resolvedTarget.y;
        out["source_index"] = f.sourceIndex;
        out["target_index"] = f.targetIndex;
        out["query_index"] = f.targetIndex;
        out["tech"] = f.tech;
        out["cost_stars"] = f.costStars;
        out["stars_before"] = f.starsBefore;
        out["affordable"] = f.affordable;
        out["building"] = static_cast<int>(a->building);
        out["spawn_type"] = static_cast<int>(a->spawnType);
        out["upgrade"] = static_cast<int>(a->upgrade);
        out["tile_action"] = static_cast<int>(a->tileAction);
        out["unit_upgrade"] = static_cast<int>(a->unitUpgrade);
        out["arg_mask"] = actionArgMaskForType(a->type);
        return out;
    }

    size_t actionSpaceSize() const {
        ensureState();
        return state_->actionSpace().size();
    }

    int currentPlayer() const {
        ensureState();
        return static_cast<int>(state_->currentPlayer());
    }

    std::vector<int> getTechs(std::optional<int> playerId) const {
        ensureState();
        const Game& g = state_->getGame();
        const int pid = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));
        if (pid < 0 || static_cast<size_t>(pid) >= g.getPlayers().size()) return {};

        const Player& p = g.getPlayer(static_cast<PlayerId>(pid));
        std::vector<int> out;
        out.reserve(p.getTechs().size());
        for (const TechId tech : p.getTechs()) {
            out.push_back(static_cast<int>(tech));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<int> seenLighthouses(std::optional<int> playerId) const {
        ensureState();
        const Game& g = state_->getGame();
        const int pid = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));
        if (pid < 0 || pid >= 16) return {};

        std::vector<int> out;
        out.reserve(4);
        for (uint8_t i = 0; i < 4; ++i) {
            if (g.hasPlayerDiscoveredLighthouse(i, static_cast<PlayerId>(pid))) {
                out.push_back(static_cast<int>(i));
            }
        }
        return out;
    }

    GameEnv clone() const {
        ensureState();
        return *this;
    }

    GameEnv saveState() const {
        ensureState();
        return *this;
    }

    void loadState(const GameEnv& snapshot) {
        *this = snapshot;
        invalidateCaches();
    }

    bool isDone() const {
        ensureState();
        return state_->isTerminal();
    }

private:
    void ensureState() const {
        if (!state_) throw std::runtime_error("GameEnv is not initialized.");
    }

    void invalidateCaches() {
        legalIdsCacheValid_ = false;
        legalIdsCachePid_ = kNoPlayer;
        legalIdsCache_.clear();
        legalMaskCacheValid_ = false;
        legalMaskCachePid_ = kNoPlayer;
        legalMaskCache_.clear();
    }

    void ensureObservationKnowledgeLayout() const {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const size_t playerCount = g.getPlayers().size();
        const size_t tileCount = static_cast<size_t>(std::max(0, m.getWidth())) * static_cast<size_t>(std::max(0, m.getHeight()));

        if (observationKnownByPlayer_.size() != playerCount) {
            const_cast<GameEnv*>(this)->initializeObservationKnowledgeFromCurrentVisibility();
            return;
        }
        for (const auto& row : observationKnownByPlayer_) {
            if (row.size() != tileCount) {
                const_cast<GameEnv*>(this)->initializeObservationKnowledgeFromCurrentVisibility();
                return;
            }
        }
    }

    void initializeObservationKnowledgeFromCurrentVisibility() {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const size_t playerCount = g.getPlayers().size();
        const size_t tileCount = static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h));

        observationKnownByPlayer_.assign(playerCount, std::vector<uint8_t>(tileCount, 0));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const int idx = y * w + x;
                const Tile& t = m.at(p);
                const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());

                for (size_t pid = 0; pid < playerCount && pid < 16; ++pid) {
                    if ((visMask & (uint16_t(1) << pid)) == 0) continue;
                    observationKnownByPlayer_[pid][static_cast<size_t>(idx)] = 1;
                }
            }
        }
    }

    void revealObservedTilesForPlayer(PlayerId pid) {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        if (pid == kNoPlayer) return;

        const size_t pidIdx = static_cast<size_t>(pid);
        if (pidIdx >= observationKnownByPlayer_.size()) return;
        auto& known = observationKnownByPlayer_[pidIdx];

        const int w = m.getWidth();
        const int h = m.getHeight();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const int idx = y * w + x;
                const Tile& t = m.at(p);
                const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());
                if ((visMask & (uint16_t(1) << pidIdx)) == 0) continue;
                known[static_cast<size_t>(idx)] = 1;
            }
        }
    }

    void revealObservedTilesForAllPlayers() {
        ensureObservationKnowledgeLayout();
        const Game& g = state_->getGame();
        for (size_t pid = 0; pid < g.getPlayers().size(); ++pid) {
            revealObservedTilesForPlayer(static_cast<PlayerId>(pid));
        }
    }

    void revealObservedTilesForAllPlayersExcept(PlayerId excluded) {
        ensureObservationKnowledgeLayout();
        const Game& g = state_->getGame();
        for (size_t pid = 0; pid < g.getPlayers().size(); ++pid) {
            if (pid == excluded) continue;
            revealObservedTilesForPlayer(static_cast<PlayerId>(pid));
        }
    }

    void ensureLegalIdsCache() const {
        const PlayerId pid = state_->currentPlayer();
        if (legalIdsCacheValid_ && legalIdsCachePid_ == pid) return;
        legalIdsCache_ = state_->legalActionIds(pid);
        legalIdsCachePid_ = pid;
        legalIdsCacheValid_ = true;
        legalMaskCacheValid_ = false;
    }

    void ensureLegalMaskCache() const {
        const PlayerId pid = state_->currentPlayer();
        if (legalMaskCacheValid_ && legalMaskCachePid_ == pid) return;

        ensureLegalIdsCache();

        legalMaskCache_.assign(state_->actionSpace().size(), 0);
        for (size_t id : legalIdsCache_) {
            if (id < legalMaskCache_.size()) legalMaskCache_[id] = 1;
        }
        legalMaskCachePid_ = pid;
        legalMaskCacheValid_ = true;
    }

    mutable std::unique_ptr<GameStateAdapter> state_;
    int mapSize_ = 16;
    std::vector<int> tribesRaw_{3, 2};
    uint32_t seed_ = 1;
    std::string unitsJsonPath_;
    std::vector<std::vector<uint8_t>> observationKnownByPlayer_;

    mutable bool legalIdsCacheValid_ = false;
    mutable PlayerId legalIdsCachePid_ = kNoPlayer;
    mutable std::vector<size_t> legalIdsCache_;
    mutable bool legalMaskCacheValid_ = false;
    mutable PlayerId legalMaskCachePid_ = kNoPlayer;
    mutable std::vector<uint8_t> legalMaskCache_;
};

} // namespace

PYBIND11_MODULE(_game_engine, m) {
    m.doc() = "Polytopia-like game engine Python bindings";

    py::enum_<TribeType>(m, "TribeType")
        .value("Unknown", TribeType::Unknown)
        .value("XinXi", TribeType::XinXi)
        .value("Imperius", TribeType::Imperius)
        .value("Bardur", TribeType::Bardur)
        .value("Kickoo", TribeType::Kickoo)
        .value("Hoodrick", TribeType::Hoodrick)
        .value("Luxidoor", TribeType::Luxidoor)
        .value("Vengir", TribeType::Vengir)
        .value("Zebasi", TribeType::Zebasi)
        .value("AiMo", TribeType::AiMo)
        .value("Quetzali", TribeType::Quetzali)
        .value("Yadakk", TribeType::Yadakk)
        .export_values();

    py::class_<GameEnv>(m, "GameEnv")
        .def(py::init<int, const std::vector<int>&, uint32_t, const std::string&>(),
             py::arg("map_size") = 16,
             py::arg("tribes") = std::vector<int>{3, 2},
             py::arg("seed") = 1u,
             py::arg("units_json_path") = "")
        .def("reset", &GameEnv::reset,
             py::arg("map_size") = std::nullopt,
             py::arg("tribes") = std::nullopt,
             py::arg("seed") = std::nullopt,
             py::arg("units_json_path") = std::nullopt)
        .def("step", &GameEnv::step,
             py::arg("action_id"),
             py::arg("reward_player") = std::nullopt)
        .def("step_fast", &GameEnv::stepFast,
             py::arg("action_id"),
             py::arg("reward_player") = std::nullopt)
        .def("step_fast_no_reveal", &GameEnv::stepFastNoReveal,
             py::arg("action_id"),
             py::arg("reward_player") = std::nullopt)
        .def("observation", &GameEnv::observation,
             py::arg("player_id") = std::nullopt,
             py::arg("visible_only") = false,
             py::arg("hidden_value") = -1)
        .def("tokenized_map", &GameEnv::tokenizedMap,
             py::arg("player_id") = std::nullopt,
             py::arg("visible_only") = false,
             py::arg("hidden_value") = -1)
        .def("lighthouse_discovered_by_masks", &GameEnv::lighthouseDiscoveredByMasks)
        .def("lighthouse_discovered_by_players", &GameEnv::lighthouseDiscoveredByPlayers)
        .def("lighthouse_visibility", &GameEnv::lighthouseVisibility,
             py::arg("player_id") = std::nullopt)
        .def("legal_action_mask", &GameEnv::legalActionMask)
        .def("legal_action_ids", &GameEnv::legalActionIds)
        .def("legal_action_ids_fast", &GameEnv::legalActionIdsFast)
        .def("action_param_spec", &GameEnv::actionParamSpec)
        .def("legal_param_actions", &GameEnv::legalParamActions)
        .def("legal_param_masks", &GameEnv::legalParamMasks)
        .def("step_param", &GameEnv::stepParam,
             py::arg("param"),
             py::arg("reward_player") = std::nullopt)
        .def("step_param_vec", &GameEnv::stepParamVec,
             py::arg("vec"),
             py::arg("reward_player") = std::nullopt)
        .def("decode_action", &GameEnv::decodeAction, py::arg("action_id"))
        .def("action_space_size", &GameEnv::actionSpaceSize)
        .def("current_player", &GameEnv::currentPlayer)
        .def("get_techs", &GameEnv::getTechs,
             py::arg("player_id") = std::nullopt)
        .def("is_done", &GameEnv::isDone)
        .def("clone", &GameEnv::clone)
        .def("save_state", &GameEnv::saveState)
        .def("load_state", &GameEnv::loadState, py::arg("snapshot"));
}
