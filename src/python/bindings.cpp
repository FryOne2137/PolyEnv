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
        , unitsJsonPath_(unitsJsonPath) {
        reset(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    }

    GameEnv(const GameEnv& other)
        : mapSize_(other.mapSize_)
        , tribesRaw_(other.tribesRaw_)
        , seed_(other.seed_)
        , unitsJsonPath_(other.unitsJsonPath_) {
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

        if (!unitsJsonPath_.empty()) {
            GameDataSystem::loadUnits(unitsJsonPath_);
        }

        Game game;
        Game::NewGameConfig cfg;
        cfg.mapSize = mapSize_;
        cfg.seed = seed_;
        cfg.tribes = parseTribes(tribesRaw_.empty() ? std::vector<int>{3, 2} : tribesRaw_);
        game.newGame(cfg);

        state_ = std::make_unique<GameStateAdapter>(std::move(game));
        return observation(std::nullopt);
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
            out["observation"] = observation(std::nullopt);
            return out;
        }

        state_->apply(*decoded);

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
        out["observation"] = observation(std::nullopt);
        return out;
    }

    py::dict observation(std::optional<int> playerId) const {
        ensureState();
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
                if (perspective >= 0 && perspective < 16) {
                    visibility[idx] = ((visMask & (uint16_t(1) << perspective)) != 0) ? 1 : 0;
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
        return obs;
    }

    std::vector<uint8_t> legalActionMask() const {
        ensureState();
        return state_->legalActionMask(state_->currentPlayer());
    }

    std::vector<size_t> legalActionIds() const {
        const auto mask = legalActionMask();
        std::vector<size_t> ids;
        ids.reserve(mask.size() / 4);
        for (size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) ids.push_back(i);
        }
        return ids;
    }

    py::dict decodeAction(size_t actionId) const {
        ensureState();
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
    }

private:
    void ensureState() const {
        if (!state_) throw std::runtime_error("GameEnv is not initialized.");
    }

    mutable std::unique_ptr<GameStateAdapter> state_;
    int mapSize_ = 16;
    std::vector<int> tribesRaw_{3, 2};
    uint32_t seed_ = 1;
    std::string unitsJsonPath_;
};

} // namespace

PYBIND11_MODULE(_game_engine, m) {
    m.doc() = "Polytopia-like game engine Python bindings";

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
        .def("observation", &GameEnv::observation, py::arg("player_id") = std::nullopt)
        .def("legal_action_mask", &GameEnv::legalActionMask)
        .def("legal_action_ids", &GameEnv::legalActionIds)
        .def("decode_action", &GameEnv::decodeAction, py::arg("action_id"))
        .def("action_space_size", &GameEnv::actionSpaceSize)
        .def("current_player", &GameEnv::currentPlayer)
        .def("clone", &GameEnv::clone)
        .def("save_state", &GameEnv::saveState)
        .def("load_state", &GameEnv::loadState, py::arg("snapshot"));
}
