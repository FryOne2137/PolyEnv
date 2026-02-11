#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ai/Action.h"
#include "ai/GameStateAdapter.h"
#include "content/tribes/Tribe.h"
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
        const auto decoded = state_->decodeActionId(actor, actionId);
        if (!decoded) {
            py::dict out;
            out["ok"] = false;
            out["done"] = state_->isTerminal();
            out["reward"] = 0.0f;
            out["observation"] = observation(std::nullopt, false, -1);
            return out;
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

        py::dict out;
        out["ok"] = true;
        out["done"] = done;
        out["reward"] = reward;
        out["winner"] = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        out["current_player"] = static_cast<int>(state_->currentPlayer());
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
        ensureObservationKnowledgeLayout();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int n = w * h;

        const int perspective = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));

        std::vector<int> terrain(n, 0);
        std::vector<int> resources(n, 0);
        std::vector<int> buildings(n, 0);
        std::vector<int> settlements(n, 0);
        std::vector<int> territoryOwner(n, -1);
        std::vector<int> visibility(n, 0);
        std::vector<int> unitOwner(n, -1);
        std::vector<int> unitType(n, 0);
        std::vector<int> unitHp(n, 0);
        const std::vector<uint8_t>* knownObs = nullptr;
        if (perspective >= 0) {
            const size_t perspectiveIdx = static_cast<size_t>(perspective);
            if (perspectiveIdx < observationKnownByPlayer_.size()) {
                knownObs = &observationKnownByPlayer_[perspectiveIdx];
            }
        }

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const int idx = y * w + x;
                const Tile& t = m.at(p);

                terrain[idx] = static_cast<int>(t.getBaseTerrain());
                resources[idx] = static_cast<int>(t.getResource());
                buildings[idx] = static_cast<int>(t.getBuildingType());
                settlements[idx] = static_cast<int>(t.getSettlementType());

                const CityId cid = t.getTerritoryCityId();
                if (cid != kNoCity) {
                    const City* c = g.getCity(cid);
                    if (c) territoryOwner[idx] = static_cast<int>(c->getOwnerId());
                }

                const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());
                bool isVisible = false;
                if (perspective >= 0 && perspective < 16) {
                    isVisible = ((visMask & (uint16_t(1) << perspective)) != 0);
                    visibility[idx] = isVisible ? 1 : 0;
                }

                bool knownToObservation = true;
                if (visibleOnly) {
                    knownToObservation = isVisible;
                    if (knownToObservation && knownObs && static_cast<size_t>(idx) < knownObs->size()) {
                        knownToObservation = ((*knownObs)[static_cast<size_t>(idx)] != 0);
                    }
                }

                if (visibleOnly && !knownToObservation) {
                    terrain[idx] = hiddenValue;
                    resources[idx] = hiddenValue;
                    buildings[idx] = hiddenValue;
                    settlements[idx] = hiddenValue;
                    territoryOwner[idx] = hiddenValue;
                    unitOwner[idx] = hiddenValue;
                    unitType[idx] = hiddenValue;
                    unitHp[idx] = hiddenValue;
                    continue;
                }

                const UnitId uid = m.unitOn(p);
                if (uid != Map::kNoUnit) {
                    const Unit* u = g.getUnit(uid);
                    if (u) {
                        unitOwner[idx] = static_cast<int>(u->getOwnerId());
                        unitType[idx] = static_cast<int>(u->getType());
                        unitHp[idx] = u->getHealth();
                    }
                }
            }
        }

        py::dict obs;
        obs["width"] = w;
        obs["height"] = h;
        obs["map_width"] = w;
        obs["map_height"] = h;
        obs["turn"] = g.getTurnNumber();
        obs["current_player"] = static_cast<int>(g.getCurrentPlayerId());
        obs["game_over"] = g.isGameOver();
        obs["winner"] = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        obs["terrain"] = std::move(terrain);
        obs["resources"] = std::move(resources);
        obs["buildings"] = std::move(buildings);
        obs["settlements"] = std::move(settlements);
        obs["territory_owner"] = std::move(territoryOwner);
        obs["visibility"] = std::move(visibility);
        obs["unit_owner"] = std::move(unitOwner);
        obs["unit_type"] = std::move(unitType);
        obs["unit_hp"] = std::move(unitHp);
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
        return obs;
    }

    std::vector<std::vector<int>> tokenizedMap() const {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int perspective = static_cast<int>(g.getCurrentPlayerId());

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

                int unitHp = -1;
                int unitOwner = -1;
                int unitId = static_cast<int>(Map::kNoUnit);

                const UnitId uid = m.unitOn(p);
                if (uid != Map::kNoUnit) {
                    unitId = static_cast<int>(uid);
                    const Unit* u = g.getUnit(uid);
                    if (u) {
                        unitHp = u->getHealth();
                        unitOwner = static_cast<int>(u->getOwnerId());
                    }
                }

                out.push_back({
                    visibility,
                    unitHp,
                    unitOwner,
                    (unitId == static_cast<int>(Map::kNoUnit)) ? -1 : unitId,
                    static_cast<int>(t.getTerritoryCityId()),
                    static_cast<int>(t.getRoadBridge()),
                    static_cast<int>(t.getBuildingType()),
                    static_cast<int>(t.getSettlementType()),
                    (static_cast<int>(t.getSettlementId()) == static_cast<int>(kNoSettlement)) ? -1 : static_cast<int>(t.getSettlementId()),
                    static_cast<int>(t.getResource()),
                    static_cast<int>(t.getBaseTerrain()),
                    static_cast<int>(t.getTribe()),
                });
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

    py::dict decodeAction(size_t actionId) const {
        ensureState();
        const Game& g = state_->getGame();
        const Map& m = g.getMap();
        const auto a = state_->decodeActionId(state_->currentPlayer(), actionId);
        if (!a) return py::dict();

        py::dict out;
        out["action_id"] = actionId;
        out["type"] = actionTypeName(a->type);
        out["pid"] = static_cast<int>(a->pid);
        out["unit"] = (a->unit == kNoUnit) ? -1 : static_cast<int>(a->unit);
        out["city"] = (a->city == kNoCity) ? -1 : static_cast<int>(a->city);
        out["pos"] = py::make_tuple(a->pos.x, a->pos.y);
        out["target"] = py::make_tuple(a->target.x, a->target.y);
        out["target_x"] = a->target.x;
        out["target_y"] = a->target.y;
        int queryIndex = -1;
        if (m.inBounds(a->target)) {
            queryIndex = a->target.y * m.getWidth() + a->target.x;
        }
        out["query_index"] = queryIndex;
        out["tech"] = static_cast<int>(a->tech);
        out["building"] = static_cast<int>(a->building);
        out["spawn_type"] = static_cast<int>(a->spawnType);
        out["upgrade"] = static_cast<int>(a->upgrade);
        out["tile_action"] = static_cast<int>(a->tileAction);
        out["unit_upgrade"] = static_cast<int>(a->unitUpgrade);
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
        .def("tokenized_map", &GameEnv::tokenizedMap)
        .def("lighthouse_discovered_by_masks", &GameEnv::lighthouseDiscoveredByMasks)
        .def("lighthouse_discovered_by_players", &GameEnv::lighthouseDiscoveredByPlayers)
        .def("lighthouse_visibility", &GameEnv::lighthouseVisibility,
             py::arg("player_id") = std::nullopt)
        .def("legal_action_mask", &GameEnv::legalActionMask)
        .def("legal_action_ids", &GameEnv::legalActionIds)
        .def("legal_action_ids_fast", &GameEnv::legalActionIdsFast)
        .def("decode_action", &GameEnv::decodeAction, py::arg("action_id"))
        .def("action_space_size", &GameEnv::actionSpaceSize)
        .def("current_player", &GameEnv::currentPlayer)
        .def("is_done", &GameEnv::isDone)
        .def("clone", &GameEnv::clone)
        .def("save_state", &GameEnv::saveState)
        .def("load_state", &GameEnv::loadState, py::arg("snapshot"));
}
