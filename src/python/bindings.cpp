#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "ai/Action.h"
#include "ai/GameStateAdapter.h"
#include "ai/GameStateSerializer.h"
#include "content/buildings/BuildingDB.h"
#include "content/tribes/Tribe.h"
#include "content/units/UnitFactory.h"
#include "game/Game.h"
#include "replay/ReplayRecorder.h"
#include "runtime/BeliefWorldBuilder.h"
#include "runtime/GameSession.h"
#include "systems/BuildingSystem.h"
#include "systems/CitySystem.h"
#include "systems/DisbandSystem.h"
#include "systems/GameDataSystem.h"
#include "systems/StarsSystem.h"
#include "world/Tile.h"
#include "ObservedEventJournal.h"

namespace py = pybind11;
using json = nlohmann::json;
using namespace polyenv_events;

namespace {

static py::object jsonToPy(const json& value) {
    if (value.is_null()) return py::none();
    if (value.is_boolean()) return py::bool_(value.get<bool>());
    if (value.is_number_integer()) return py::int_(value.get<long long>());
    if (value.is_number_unsigned()) return py::int_(value.get<unsigned long long>());
    if (value.is_number_float()) return py::float_(value.get<double>());
    if (value.is_string()) return py::str(value.get<std::string>());
    if (value.is_array()) {
        py::list out;
        for (const auto& item : value) out.append(jsonToPy(item));
        return std::move(out);
    }
    if (value.is_object()) {
        py::dict out;
        for (auto it = value.begin(); it != value.end(); ++it) {
            out[py::str(it.key())] = jsonToPy(it.value());
        }
        return std::move(out);
    }
    return py::none();
}

template <typename T>
static py::array_t<T> makeArray1D(size_t n) {
    return py::array_t<T>({static_cast<py::ssize_t>(n)});
}

template <typename T>
static py::array_t<T> makeArray2D(size_t rows, size_t cols) {
    return py::array_t<T>({
        static_cast<py::ssize_t>(rows),
        static_cast<py::ssize_t>(cols),
    });
}

// Bind caller-owned output arrays without coercion or copying. This is the
// boundary used by reset_into()/step_into(): an external training repository
// can supply NumPy views of framework-pinned host tensors while PolyEnv stays
// independent from PyTorch, CUDA, and any GPU lifetime rules.
template <typename T>
static py::array_t<T> requireWritableBatchArray(
    const py::dict& buffers,
    const char* name,
    std::initializer_list<py::ssize_t> expectedShape)
{
    const py::str key(name);
    if (!buffers.contains(key)) {
        throw std::invalid_argument(std::string("buffers is missing required array '") + name + "'");
    }
    py::handle item = buffers[key];
    if (!py::isinstance<py::array>(item)) {
        throw std::invalid_argument(std::string("buffers['") + name + "'] must be a NumPy array");
    }
    const py::array array = py::reinterpret_borrow<py::array>(item);
    if (!array.dtype().is(py::dtype::of<T>())) {
        throw std::invalid_argument(std::string("buffers['") + name + "'] has an incorrect dtype");
    }
    if (array.ndim() != static_cast<py::ssize_t>(expectedShape.size())) {
        throw std::invalid_argument(std::string("buffers['") + name + "'] has an incorrect rank");
    }
    size_t axis = 0;
    for (const py::ssize_t expected : expectedShape) {
        if (array.shape(static_cast<py::ssize_t>(axis)) != expected) {
            throw std::invalid_argument(std::string("buffers['") + name + "'] has an incorrect shape");
        }
        ++axis;
    }
    if ((array.flags() & py::array::c_style) != py::array::c_style) {
        throw std::invalid_argument(std::string("buffers['") + name + "'] must be C-contiguous");
    }
    if (!py::cast<bool>(array.attr("flags").attr("writeable"))) {
        throw std::invalid_argument(std::string("buffers['") + name + "'] must be writable");
    }
    if (!py::cast<bool>(array.attr("flags").attr("aligned"))) {
        throw std::invalid_argument(std::string("buffers['") + name + "'] must be aligned");
    }
    return py::reinterpret_borrow<py::array_t<T>>(array);
}

// A single output batch must never contain overlapping writable storage. The
// encoder writes many fields concurrently; accepting aliases would otherwise
// make the result non-deterministic and can corrupt a caller's pinned slot.
template <size_t N>
static void validateNonOverlappingBatchStorage(
    const std::array<std::pair<const char*, const py::array*>, N>& arrays)
{
    for (size_t left = 0; left < arrays.size(); ++left) {
        const size_t leftBytes = arrays[left].second->nbytes();
        if (leftBytes == 0) continue;
        const uintptr_t leftBegin = reinterpret_cast<uintptr_t>(arrays[left].second->data());
        const uintptr_t leftEnd = leftBegin + leftBytes;
        if (leftEnd < leftBegin) throw std::overflow_error("batch buffer address range overflow");
        for (size_t right = left + 1; right < arrays.size(); ++right) {
            const size_t rightBytes = arrays[right].second->nbytes();
            if (rightBytes == 0) continue;
            const uintptr_t rightBegin = reinterpret_cast<uintptr_t>(arrays[right].second->data());
            const uintptr_t rightEnd = rightBegin + rightBytes;
            if (rightEnd < rightBegin) throw std::overflow_error("batch buffer address range overflow");
            if (leftBegin < rightEnd && rightBegin < leftEnd) {
                throw std::invalid_argument(
                    std::string("buffers['") + arrays[left].first + "'] overlaps buffers['" +
                    arrays[right].first + "']");
            }
        }
    }
}

static py::array_t<int32_t> mapTokensToNumpy(const json& mapTokens) {
    const size_t rows = mapTokens.is_array() ? mapTokens.size() : 0;
    const size_t cols = rows > 0 && mapTokens[0].is_array() ? mapTokens[0].size() : 0;
    py::array_t<int32_t> arr = makeArray2D<int32_t>(rows, cols);
    auto view = arr.mutable_unchecked<2>();
    for (size_t r = 0; r < rows; ++r) {
        const json& row = mapTokens[r];
        for (size_t c = 0; c < cols; ++c) {
            view(static_cast<py::ssize_t>(r), static_cast<py::ssize_t>(c)) =
                row[c].is_number_float()
                    ? static_cast<int32_t>(row[c].get<double>())
                    : row[c].get<int32_t>();
        }
    }
    return arr;
}

static py::array_t<int32_t> mapRowsToNumpy(const std::vector<std::vector<int>>& rowsVec) {
    const size_t rows = rowsVec.size();
    const size_t cols = rows > 0 ? rowsVec[0].size() : 0;
    py::array_t<int32_t> arr({rows, cols});
    auto out = arr.mutable_unchecked<2>();
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            out(r, c) = (c < rowsVec[r].size()) ? static_cast<int32_t>(rowsVec[r][c]) : int32_t(0);
        }
    }
    return arr;
}

static std::vector<std::vector<int>> completedMapTokensFromPy(const py::object& value) {
    if (py::isinstance<py::array>(value)) {
        auto array = py::array_t<int64_t, py::array::c_style | py::array::forcecast>::ensure(value);
        if (!array || array.ndim() != 2) {
            throw std::invalid_argument("completed_map_tokens must be a two-dimensional integer array");
        }
        const py::ssize_t rows = array.shape(0);
        const py::ssize_t cols = array.shape(1);
        std::vector<std::vector<int>> result(static_cast<size_t>(rows), std::vector<int>(static_cast<size_t>(cols)));
        auto view = array.unchecked<2>();
        for (py::ssize_t row = 0; row < rows; ++row) {
            for (py::ssize_t col = 0; col < cols; ++col) {
                const int64_t item = view(row, col);
                if (item < std::numeric_limits<int>::min() || item > std::numeric_limits<int>::max()) {
                    throw std::invalid_argument("completed_map_tokens contains an out-of-range integer");
                }
                result[static_cast<size_t>(row)][static_cast<size_t>(col)] = static_cast<int>(item);
            }
        }
        return result;
    }
    return value.cast<std::vector<std::vector<int>>>();
}

static py::array_t<int64_t> actionFieldToNumpy(
    const json& actions,
    const char* field,
    int64_t defaultValue = -1)
{
    const size_t n = actions.is_array() ? actions.size() : 0;
    py::array_t<int64_t> arr = makeArray1D<int64_t>(n);
    auto view = arr.mutable_unchecked<1>();
    for (size_t i = 0; i < n; ++i) {
        const json& a = actions[i];
        view(static_cast<py::ssize_t>(i)) =
            a.contains(field) && a[field].is_number_integer()
                ? a[field].get<int64_t>()
                : defaultValue;
    }
    return arr;
}

static py::array_t<uint8_t> actionArgMaskToNumpy(const json& actions) {
    const size_t rows = actions.is_array() ? actions.size() : 0;
    size_t cols = 0;
    if (rows > 0 && actions[0].contains("arg_mask") && actions[0]["arg_mask"].is_array()) {
        cols = actions[0]["arg_mask"].size();
    }
    py::array_t<uint8_t> arr = makeArray2D<uint8_t>(rows, cols);
    auto view = arr.mutable_unchecked<2>();
    for (size_t r = 0; r < rows; ++r) {
        const json& mask = actions[r].contains("arg_mask") ? actions[r]["arg_mask"] : json::array();
        for (size_t c = 0; c < cols; ++c) {
            view(static_cast<py::ssize_t>(r), static_cast<py::ssize_t>(c)) =
                c < mask.size() ? static_cast<uint8_t>(mask[c].get<int>()) : 0;
        }
    }
    return arr;
}

static py::dict actionsToNumpy(const json& actions) {
    py::dict out;
    out["action_id"] = actionFieldToNumpy(actions, "action_id");
    out["type_id"] = actionFieldToNumpy(actions, "type_id");
    out["source_index"] = actionFieldToNumpy(actions, "source_index");
    out["target_index"] = actionFieldToNumpy(actions, "target_index");
    out["city"] = actionFieldToNumpy(actions, "city");
    out["tech"] = actionFieldToNumpy(actions, "tech");
    out["building"] = actionFieldToNumpy(actions, "building");
    out["spawn_type"] = actionFieldToNumpy(actions, "spawn_type");
    out["upgrade"] = actionFieldToNumpy(actions, "upgrade");
    out["tile_action"] = actionFieldToNumpy(actions, "tile_action");
    out["unit_upgrade"] = actionFieldToNumpy(actions, "unit_upgrade");
    out["unit_id"] = actionFieldToNumpy(actions, "unit_id");
    out["unit"] = actionFieldToNumpy(actions, "unit");
    out["cost_stars"] = actionFieldToNumpy(actions, "cost_stars");
    out["stars_before"] = actionFieldToNumpy(actions, "stars_before");
    out["affordable"] = actionFieldToNumpy(actions, "affordable", 0);
    out["damage_dealt"] = actionFieldToNumpy(actions, "damage_dealt");
    out["damage_received"] = actionFieldToNumpy(actions, "damage_received");
    out["arg_mask"] = actionArgMaskToNumpy(actions);

    py::list types;
    py::list typeFullnames;
    for (const auto& action : actions) {
        types.append(py::str(action.value("type", "")));
        typeFullnames.append(py::str(action.value("type_fullname", "")));
    }
    out["type"] = std::move(types);
    out["type_fullname"] = std::move(typeFullnames);
    return out;
}

static std::string resolveDefaultUnitsJsonPath() {
    // Prefer package data path regardless of current working directory.
    try {
        py::module_ importlibResources = py::module_::import("importlib.resources");
        py::object packageRoot = importlibResources.attr("files")("PolyEnv");
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
    // Player visibility is represented by a 16-bit mask throughout the
    // engine. Reject unsupported counts at the public boundary instead of
    // constructing a game in which a later player cannot observe tiles.
    if (tribes.size() < 2 || tribes.size() > 16) {
        throw std::invalid_argument("tribes must contain between 2 and 16 players");
    }
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

static std::string actionBaseTypeName(Action::Type t) {
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

static std::string tileActionName(Action::TileActionKind kind) {
    switch (kind) {
        case Action::TileActionKind::Hunt: return "hunt";
        case Action::TileActionKind::Organization: return "organization";
        case Action::TileActionKind::Fishing: return "fishing";
        case Action::TileActionKind::ClearForest: return "clear_forest";
        case Action::TileActionKind::BurnForest: return "burn_forest";
        case Action::TileActionKind::GrowForest: return "grow_forest";
        case Action::TileActionKind::DestroyTile: return "destroy_tile";
        case Action::TileActionKind::BuildRoad: return "build_road";
        case Action::TileActionKind::BuildBridge: return "build_bridge";
        case Action::TileActionKind::Explorer: return "explorer";
        case Action::TileActionKind::FoundCity: return "found_city";
        case Action::TileActionKind::Ruin: return "ruin";
        case Action::TileActionKind::Starfish: return "starfish";
        case Action::TileActionKind::CaptureCity: return "capture_city";
        case Action::TileActionKind::None: return "none";
    }
    return "none";
}

static std::string unitUpgradeName(Action::UnitUpgradeKind kind) {
    switch (kind) {
        case Action::UnitUpgradeKind::RaftToScout: return "raft_to_scout";
        case Action::UnitUpgradeKind::RaftToRammer: return "raft_to_rammer";
        case Action::UnitUpgradeKind::RaftToBomber: return "raft_to_bomber";
        case Action::UnitUpgradeKind::BecomeVeteran: return "become_veteran";
        case Action::UnitUpgradeKind::Disband: return "disband";
        case Action::UnitUpgradeKind::None: return "none";
    }
    return "none";
}

static std::string buildingTypeName(BuildingTypeEnum b) {
    switch (b) {
        case BuildingTypeEnum::Farm: return "farm";
        case BuildingTypeEnum::Forge: return "forge";
        case BuildingTypeEnum::LumberHut: return "lumber_hut";
        case BuildingTypeEnum::Market: return "market";
        case BuildingTypeEnum::Mine: return "mine";
        case BuildingTypeEnum::Port: return "port";
        case BuildingTypeEnum::Sawmill: return "sawmill";
        case BuildingTypeEnum::Windmill: return "windmill";
        case BuildingTypeEnum::AltarOfPeace: return "altar_of_peace";
        case BuildingTypeEnum::EmperorsTomb: return "emperors_tomb";
        case BuildingTypeEnum::EyeOfGod: return "eye_of_god";
        case BuildingTypeEnum::GateOfPower: return "gate_of_power";
        case BuildingTypeEnum::GrandBazaar: return "grand_bazaar";
        case BuildingTypeEnum::ParkOfFortune: return "park_of_fortune";
        case BuildingTypeEnum::TowerOfWisdom: return "tower_of_wisdom";
        case BuildingTypeEnum::Lighthouse: return "lighthouse";
        case BuildingTypeEnum::None: return "none";
    }
    return "none";
}

static std::string techIdName(TechId t) {
    switch (t) {
        case TechId::Climbing: return "climbing";
        case TechId::Fishing: return "fishing";
        case TechId::Hunting: return "hunting";
        case TechId::Organization: return "organization";
        case TechId::Riding: return "riding";
        case TechId::Archery: return "archery";
        case TechId::Ramming: return "ramming";
        case TechId::Farming: return "farming";
        case TechId::Forestry: return "forestry";
        case TechId::FreeSpirit: return "free_spirit";
        case TechId::Meditation: return "meditation";
        case TechId::Mining: return "mining";
        case TechId::Roads: return "roads";
        case TechId::Sailing: return "sailing";
        case TechId::Strategy: return "strategy";
        case TechId::Aquatism: return "aquatism";
        case TechId::Chivalry: return "chivalry";
        case TechId::Construction: return "construction";
        case TechId::Diplomacy: return "diplomacy";
        case TechId::Mathematics: return "mathematics";
        case TechId::Navigation: return "navigation";
        case TechId::Philosophy: return "philosophy";
        case TechId::Smithery: return "smithery";
        case TechId::Spiritualism: return "spiritualism";
        case TechId::Trade: return "trade";
        case TechId::Count: return "none";
    }
    return "none";
}

static std::string cityUpgradeChoiceName(CityUpgradeChoice c) {
    switch (c) {
        case CityUpgradeChoice::Workshop: return "workshop";
        case CityUpgradeChoice::Explorer: return "explorer";
        case CityUpgradeChoice::CityWall: return "city_wall";
        case CityUpgradeChoice::Resources: return "resources";
        case CityUpgradeChoice::PopulationGrowth: return "population_growth";
        case CityUpgradeChoice::BorderGrowth: return "border_growth";
        case CityUpgradeChoice::Park: return "park";
        case CityUpgradeChoice::SuperUnit: return "super_unit";
        case CityUpgradeChoice::None: return "none";
    }
    return "none";
}

static std::string unitTypeName(UnitType u) {
    switch (u) {
        case UnitType::Warrior: return "warrior";
        case UnitType::Archer: return "archer";
        case UnitType::Defender: return "defender";
        case UnitType::Rider: return "rider";
        case UnitType::MindBender: return "mind_bender";
        case UnitType::Swordsman: return "swordsman";
        case UnitType::Catapult: return "catapult";
        case UnitType::Cloak: return "cloak";
        case UnitType::Knight: return "knight";
        case UnitType::Dagger: return "dagger";
        case UnitType::Giant: return "giant";
        case UnitType::Bunny: return "bunny";
        case UnitType::Bunta: return "bunta";
        case UnitType::Raft: return "raft";
        case UnitType::Scout: return "scout";
        case UnitType::Rammer: return "rammer";
        case UnitType::Bomber: return "bomber";
        case UnitType::Dinghy: return "dinghy";
        case UnitType::Pirate: return "pirate";
        case UnitType::Juggernaut: return "juggernaut";
        case UnitType::Mermaid: return "mermaid";
        case UnitType::AquaticAmphibian: return "aquatic_amphibian";
        case UnitType::MermaidArcher: return "mermaid_archer";
        case UnitType::MermaidDefender: return "mermaid_defender";
        case UnitType::Swordsmaid: return "swordsmaid";
        case UnitType::Scuba: return "scuba";
        case UnitType::Siren: return "siren";
        case UnitType::Shark: return "shark";
        case UnitType::YellyBelly: return "yelly_belly";
        case UnitType::Puffer: return "puffer";
        case UnitType::TridentionAq: return "tridention_aq";
        case UnitType::CrabAq: return "crab_aq";
        case UnitType::Polytaur: return "polytaur";
        case UnitType::DragonEgg: return "dragon_egg";
        case UnitType::BabyDragon: return "baby_dragon";
        case UnitType::FireDragon: return "fire_dragon";
        case UnitType::IceArcher: return "ice_archer";
        case UnitType::BattleSled: return "battle_sled";
        case UnitType::Mooni: return "mooni";
        case UnitType::IceFortress: return "ice_fortress";
        case UnitType::Gaami: return "gaami";
        case UnitType::Hexapod: return "hexapod";
        case UnitType::Kiton: return "kiton";
        case UnitType::Phychi: return "phychi";
        case UnitType::Shaman: return "shaman";
        case UnitType::Raychi: return "raychi";
        case UnitType::Exida: return "exida";
        case UnitType::Doomux: return "doomux";
        case UnitType::MothC: return "moth_c";
        case UnitType::LarvaC: return "larva_c";
        case UnitType::InsectEgg: return "insect_egg";
        case UnitType::Boomchi: return "boomchi";
        case UnitType::LivingIsland: return "living_island";
        case UnitType::GiantSuper: return "giant_super";
        case UnitType::Unknown: return "unknown";
    }
    return "unknown";
}

static std::string actionFullTypeName(const Action& a) {
    switch (a.type) {
        case Action::Type::BuyTech:
            return "buy_tech:" + techIdName(a.tech);
        case Action::Type::UpgradeCity:
            return "upgrade_city:" + cityUpgradeChoiceName(a.upgrade);
        case Action::Type::Build:
            return "build:" + buildingTypeName(a.building);
        case Action::Type::SpawnUnit:
            return "spawn_unit:" + unitTypeName(a.spawnType);
        case Action::Type::TileAction:
            return "tile_action:" + tileActionName(a.tileAction);
        case Action::Type::UnitUpgrade:
            return "unit_upgrade:" + unitUpgradeName(a.unitUpgrade);
        default:
            return actionBaseTypeName(a.type);
    }
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
        case Action::TileActionKind::Starfish: return TechId::Navigation;
        case Action::TileActionKind::Explorer:
        case Action::TileActionKind::FoundCity:
        case Action::TileActionKind::Ruin:
        case Action::TileActionKind::CaptureCity:
        case Action::TileActionKind::None:
            return TechId::Count;
    }
    return TechId::Count;
}

static TechId requiredTechForTileAction(const Game& game, const Action& action) {
    if (action.tileAction == Action::TileActionKind::DestroyTile &&
        game.getMap().inBounds(action.pos) &&
        game.getMap().at(action.pos).getRoadBridge() == RoadBridgeEnum::Bridge) {
        return TechId::Construction;
    }
    return requiredTechForTileAction(action.tileAction);
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
        case Action::UnitUpgradeKind::Disband:
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
        case Action::UnitUpgradeKind::Disband: return TechId::FreeSpirit;
        case Action::UnitUpgradeKind::BecomeVeteran:
        case Action::UnitUpgradeKind::None:
            return TechId::Count;
    }
    return TechId::Count;
}

static bool tileActionUsesUnit(Action::TileActionKind kind) {
    switch (kind) {
        case Action::TileActionKind::FoundCity:
        case Action::TileActionKind::Ruin:
        case Action::TileActionKind::Starfish:
        case Action::TileActionKind::CaptureCity:
            return true;
        default:
            return false;
    }
}

static constexpr int kActionTypeCount = static_cast<int>(Action::Type::UnitUpgrade) + 1;
static constexpr int kTechVocabSize = static_cast<int>(TechId::Count) + 1; // + "none"/Count sentinel
static constexpr int kBuildingVocabSize = static_cast<int>(BuildingTypeEnum::Lighthouse) + 1;
static constexpr int kSpawnTypeVocabSize = static_cast<int>(UnitType::GiantSuper) + 1;
static constexpr int kCityUpgradeVocabSize = static_cast<int>(CityUpgradeChoice::SuperUnit) + 1;
static constexpr int kTileActionVocabSize = static_cast<int>(Action::TileActionKind::CaptureCity) + 1;
static constexpr int kUnitUpgradeVocabSize = static_cast<int>(Action::UnitUpgradeKind::Disband) + 1;
static constexpr int kPopulationGainVocabSize = 256;
static constexpr int kMapTokenFeatureCount = 23;
static constexpr int kVectorStateFeatureCount = 11;
static constexpr int kVectorActionFeatureCount = 17;
static constexpr int kVectorActionArgMaskCount = 12;
// A fixed-size visible-event window keeps vector batches dense and GPU-ready.
// Event sequence ids stay in separate uint64 arrays so they never lose
// precision in the int32 model feature tensor.
static constexpr int kVisibleEventFeatureCount = 26;
static constexpr int kVisibleEventAffectedUnitCount = 9;
static constexpr int kVisibleEventAffectedFeatureCount = 8;

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
    int unitId = -1;
    int populationGain = 0;
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
    bool expectsUnit = false;

    switch (a.type) {
        case Action::Type::Move:
        case Action::Type::Attack:
            sourcePos = unitPos(a.unit, a.pos);
            hasSource = true;
            hasTarget = true;
            expectsUnit = true;
            break;
        case Action::Type::Build:
            sourcePos = a.pos;
            targetPos = a.pos;
            hasSource = true;
            hasTarget = true;
            f.tech = static_cast<int>(BuildingDB::getRequiredTech(a.building));
            f.building = static_cast<int>(a.building);
            f.populationGain = BuildingSystem::estimatePopulationGainForCity(g, a.pid, a.pos, a.building);
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
            expectsUnit = tileActionUsesUnit(a.tileAction);
            f.tileAction = static_cast<int>(a.tileAction);
            f.tech = static_cast<int>(requiredTechForTileAction(g, a));
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
            expectsUnit = true;
            break;
        case Action::Type::UnitUpgrade:
            sourcePos = unitPos(a.unit, a.pos);
            targetPos = sourcePos;
            hasSource = true;
            expectsUnit = true;
            f.unitUpgrade = static_cast<int>(a.unitUpgrade);
            f.tech = static_cast<int>(requiredTechForUnitUpgrade(a.unitUpgrade));
            f.costStars = starsCostForUnitUpgrade(a.unitUpgrade);
            if (a.unitUpgrade == Action::UnitUpgradeKind::Disband) {
                f.costStars = -DisbandSystem::refundStars(g, a.unit);
            }
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

    if (expectsUnit) {
        if (a.unit != kNoUnit) {
            f.unitId = static_cast<int>(a.unit);
        } else if (hasSource && m.inBounds(sourcePos)) {
            const UnitId uid = m.unitOn(sourcePos);
            if (uid != Map::kNoUnit) {
                f.unitId = static_cast<int>(uid);
            }
        }
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

static std::vector<uint8_t> actionArgMaskForType(const Action& a) {
    // [source, target, tech, building, spawn_type, city_upgrade, tile_action, unit_upgrade, unit_id, damage_dealt, damage_received, population_gain]
    std::vector<uint8_t> mask(12, 0);
    switch (a.type) {
        case Action::Type::Move:
            mask[0] = 1;
            mask[1] = 1;
            mask[8] = 1;
            break;
        case Action::Type::Attack:
            mask[0] = 1;
            mask[1] = 1;
            mask[9] = 1;
            mask[10] = 1;
            mask[8] = 1;
            break;
        case Action::Type::Heal:
            mask[0] = 1;
            mask[8] = 1;
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
            mask[11] = 1;
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
            if (tileActionUsesUnit(a.tileAction)) {
                mask[8] = 1;
            }
            break;
        case Action::Type::UnitUpgrade:
            mask[0] = 1;
            mask[2] = 1;
            mask[7] = 1;
            mask[8] = 1;
            break;
    }
    return mask;
}

static void writeActionArgMask(const Action& a, uint8_t* out) {
    std::fill_n(out, 12, uint8_t{0});
    switch (a.type) {
        case Action::Type::Move:
            out[0] = out[1] = out[8] = 1;
            break;
        case Action::Type::Attack:
            out[0] = out[1] = out[8] = out[9] = out[10] = 1;
            break;
        case Action::Type::Heal:
            out[0] = out[8] = 1;
            break;
        case Action::Type::BuyTech:
            out[2] = 1;
            break;
        case Action::Type::UpgradeCity:
            out[0] = out[5] = 1;
            break;
        case Action::Type::Build:
            out[0] = out[1] = out[2] = out[3] = out[11] = 1;
            break;
        case Action::Type::SpawnUnit:
            out[0] = out[1] = out[2] = out[4] = 1;
            break;
        case Action::Type::TileAction:
            out[0] = out[1] = out[2] = out[6] = 1;
            if (tileActionUsesUnit(a.tileAction)) out[8] = 1;
            break;
        case Action::Type::UnitUpgrade:
            out[0] = out[2] = out[7] = out[8] = 1;
            break;
        case Action::Type::EndTurn:
            break;
    }
}

struct NativeStepResult {
    bool ok = false;
    bool done = false;
    float reward = 0.0f;
    int winner = -1;
    int currentPlayer = -1;
};

class GameEnv {
public:
    // A fixed-size model batch may represent only a prefix of a wide legal
    // action set. Keep the selection allocation-free and deterministic: retain
    // the canonical first rows, but reserve the final row for EndTurn when it
    // would otherwise be dropped. This guarantees that truncation never leaves
    // a learner without a progress action.
    struct ModelActionWindow {
        const std::vector<size_t>& legalIds;
        size_t totalCount = 0;
        size_t count = 0;
        size_t retainedEndTurnActionId = 0;
        bool replacesFinalRow = false;

        size_t actionId(size_t row) const {
            if (row >= count) throw std::out_of_range("model action row is out of range");
            return replacesFinalRow && row + 1 == count ? retainedEndTurnActionId : legalIds[row];
        }

        bool truncated() const { return count < totalCount; }
    };

    GameEnv(int mapSize,
            const std::vector<int>& tribes,
            uint32_t seed,
            const std::string& unitsJsonPath,
            MapType mapType)
        : mapSize_(mapSize)
        , tribesRaw_(tribes)
        , seed_(seed)
        , unitsJsonPath_(unitsJsonPath.empty() ? resolveDefaultUnitsJsonPath() : unitsJsonPath)
        , mapType_(mapType) {
        resetCore(true);
    }

    GameEnv(const GameEnv& other)
        : mapSize_(other.mapSize_)
        , tribesRaw_(other.tribesRaw_)
        , seed_(other.seed_)
        , unitsJsonPath_(other.unitsJsonPath_)
        , mapType_(other.mapType_)
        , legalIdsCacheValid_(other.legalIdsCacheValid_)
        , legalIdsCachePid_(other.legalIdsCachePid_)
        , legalIdsCache_(other.legalIdsCache_)
        , legalMaskCacheValid_(other.legalMaskCacheValid_)
        , legalMaskCachePid_(other.legalMaskCachePid_)
        , legalMaskCache_(other.legalMaskCache_)
        , session_(other.session_ ? other.session_->clone() : nullptr) {}

    GameEnv& operator=(const GameEnv& other) {
        if (this == &other) return *this;
        mapSize_ = other.mapSize_;
        tribesRaw_ = other.tribesRaw_;
        seed_ = other.seed_;
        unitsJsonPath_ = other.unitsJsonPath_;
        mapType_ = other.mapType_;
        legalIdsCacheValid_ = other.legalIdsCacheValid_;
        legalIdsCachePid_ = other.legalIdsCachePid_;
        legalIdsCache_ = other.legalIdsCache_;
        legalMaskCacheValid_ = other.legalMaskCacheValid_;
        legalMaskCachePid_ = other.legalMaskCachePid_;
        legalMaskCache_ = other.legalMaskCache_;
        session_ = other.session_ ? other.session_->clone() : nullptr;
        return *this;
    }

    GameEnv(GameEnv&&) noexcept = default;
    GameEnv& operator=(GameEnv&&) noexcept = default;

    py::dict reset(std::optional<int> mapSize,
                   std::optional<std::vector<int>> tribes,
                   std::optional<uint32_t> seed,
                   std::optional<std::string> unitsJsonPath,
                   std::optional<MapType> mapType) {
        if (mapSize) mapSize_ = *mapSize;
        if (tribes) tribesRaw_ = *tribes;
        if (seed) seed_ = *seed;
        if (unitsJsonPath) unitsJsonPath_ = *unitsJsonPath;
        if (mapType) mapType_ = *mapType;

        resetCore(true);
        return observation(std::nullopt, true, -1);
    }

    // Used only by VectorGameEnv. Unit templates are loaded before its worker
    // pool starts, so this path never mutates shared game data from a worker.
    void resetNative(uint32_t seed) {
        seed_ = seed;
        resetCore(false);
    }

    NativeStepResult stepNative(size_t actionId, std::optional<int> rewardPlayer = std::nullopt) {
        ensureState();
        const PlayerId actor = session_->state.currentPlayer();
        const auto decoded = session_->state.decodeActionId(actor, actionId);
        if (!decoded) {
            return {false, session_->state.isTerminal(), 0.0f, -1,
                    static_cast<int>(session_->state.currentPlayer())};
        }

        session_->apply(*decoded, actionId);
        revealObservedTilesForAllPlayers();
        invalidateCaches();

        const bool done = session_->state.isTerminal();
        const Game& g = session_->state.getGame();
        const int rewardPlayerId = rewardPlayer.value_or(static_cast<int>(actor));
        float reward = 0.0f;
        const PlayerId terminalWinner = done ? terminalWinnerNative() : kNoPlayer;
        if (done && rewardPlayerId >= 0 && rewardPlayerId < static_cast<int>(g.getPlayers().size())) {
            reward = terminalValueForPlayer(static_cast<PlayerId>(rewardPlayerId));
        }
        return {true, done, reward, terminalWinner == kNoPlayer ? -1 : static_cast<int>(terminalWinner),
                static_cast<int>(session_->state.currentPlayer())};
    }

    // Native MCTS branches preserve authoritative state and fog-of-war
    // knowledge while discarding replay/event history that is irrelevant to
    // its dense model packet. This prevents each tree node from copying its
    // complete path history.
    GameEnv cloneForMcts() const {
        ensureState();
        return GameEnv(session_->cloneForSearch(), mapSize_, tribesRaw_, seed_, unitsJsonPath_, mapType_);
    }

    GameEnv mctsChild(size_t actionId) const {
        GameEnv child = cloneForMcts();
        const NativeStepResult result = child.stepNative(actionId);
        if (!result.ok) {
            throw std::logic_error("MCTS attempted to expand a non-legal action id");
        }
        return child;
    }

    size_t legalActionCountNative() const {
        ensureState();
        const PlayerId pid = session_->state.currentPlayer();
        return session_->state.legalActionIdsRef(pid).size();
    }

    const std::vector<size_t>& legalActionIdsRefNative() const {
        ensureState();
        const PlayerId pid = session_->state.currentPlayer();
        return session_->state.legalActionIdsRef(pid);
    }

    ModelActionWindow modelActionWindow(size_t maxActions) const {
        ensureState();
        if (maxActions == 0) throw std::invalid_argument("max_actions must be positive");

        const PlayerId pid = session_->state.currentPlayer();
        const auto& legalIds = session_->state.legalActionIdsRef(pid);
        const size_t count = std::min(maxActions, legalIds.size());
        ModelActionWindow out{legalIds, legalIds.size(), count, 0, false};
        if (legalIds.size() <= maxActions) return out;

        // The first `maxActions - 1` canonical rows are retained. Look only
        // beyond that prefix for EndTurn; if it is already in the prefix, the
        // natural ordering is unchanged.
        for (size_t index = maxActions; index < legalIds.size(); ++index) {
            const auto action = session_->state.decodeActionId(pid, legalIds[index]);
            if (action && action->type == Action::Type::EndTurn) {
                out.retainedEndTurnActionId = legalIds[index];
                out.replacesFinalRow = true;
                break;
            }
        }
        return out;
    }

    bool isTerminalNative() const {
        ensureState();
        return session_->state.isTerminal();
    }

    PlayerId currentPlayerNative() const {
        ensureState();
        return session_->state.currentPlayer();
    }

    int playerCountNative() const {
        ensureState();
        return static_cast<int>(session_->state.getGame().getPlayers().size());
    }

    int mapSizeNative() const { return mapSize_; }

    // Resolve the terminal winner once so MctsPool can back up a terminal
    // result from every player's perspective without consulting hidden state.
    // A game can also be terminal before Game::winner has been set; preserve
    // the existing sole-survivor fallback for that case.
    PlayerId terminalWinnerNative() const {
        ensureState();
        if (!session_->state.isTerminal()) {
            throw std::logic_error("terminal winner requested for a non-terminal MCTS node");
        }

        const Game& game = session_->state.getGame();
        PlayerId winner = kNoPlayer;
        if (game.isGameOver()) {
            winner = game.getWinner();
        } else {
            int alive = 0;
            for (size_t index = 0; index < game.getPlayers().size(); ++index) {
                const Player& player = game.getPlayer(static_cast<PlayerId>(index));
                if (!player.getCities().empty() || !player.getUnits().empty()) {
                    ++alive;
                    winner = static_cast<PlayerId>(index);
                }
            }
            if (alive != 1) winner = kNoPlayer;
        }

        return winner;
    }

    // Terminal rewards are zero-sum across the player vector: the winner gets
    // +1 and each loser shares -1. For two players this preserves +1/-1.
    float terminalValueForPlayer(PlayerId player) const {
        const PlayerId winner = terminalWinnerNative();
        if (winner == kNoPlayer) return 0.0f;
        if (winner == player) return 1.0f;
        const int players = playerCountNative();
        return -1.0f / static_cast<float>(players - 1);
    }

    // This scalar helper is the hot terminal path for two-player, zero-sum
    // MCTS. Multi-player MCTS resolves terminal utilities once in MctsPool.
    float terminalValueForCurrentPlayer() const {
        return terminalValueForPlayer(session_->state.currentPlayer());
    }

    py::dict step(size_t actionId, std::optional<int> rewardPlayer) {
        ensureState();
        const PlayerId actor = session_->state.currentPlayer();
        const Game& gBefore = session_->state.getGame();
        const int starsBefore = gBefore.getPlayer(actor).getStars();
        const auto decoded = session_->state.decodeActionId(actor, actionId);
        if (!decoded) {
            py::dict out;
            out["ok"] = false;
            out["done"] = session_->state.isTerminal();
            out["reward"] = 0.0f;
            out["selected_action_id"] = static_cast<int>(actionId);
            out["stars_before"] = starsBefore;
            out["stars_after"] = starsBefore;
            out["delta_stars"] = 0;
            out["action_cost_stars"] = -1;
            out["action_affordable"] = 0;
            out["observation"] = observation(std::nullopt, true, -1);
            return out;
        }
        const ActionModelFields actionFeatures = buildActionModelFields(gBefore, *decoded);
        session_->apply(*decoded, actionId);
        revealObservedTilesForAllPlayers();
        invalidateCaches();

        const bool done = session_->state.isTerminal();
        const Game& g = session_->state.getGame();
        const int starsAfter = g.getPlayer(actor).getStars();
        int rp = rewardPlayer.value_or(static_cast<int>(actor));
        float reward = 0.0f;
        const PlayerId terminalWinner = done ? terminalWinnerNative() : kNoPlayer;
        if (done && rp >= 0 && rp < static_cast<int>(g.getPlayers().size())) {
            reward = terminalValueForPlayer(static_cast<PlayerId>(rp));
        }

        py::dict out;
        out["ok"] = true;
        out["done"] = done;
        out["reward"] = reward;
        out["winner"] = terminalWinner == kNoPlayer ? -1 : static_cast<int>(terminalWinner);
        out["current_player"] = static_cast<int>(session_->state.currentPlayer());
        out["selected_action_id"] = static_cast<int>(actionId);
        out["stars_before"] = starsBefore;
        out["stars_after"] = starsAfter;
        out["delta_stars"] = starsAfter - starsBefore;
        out["action_cost_stars"] = actionFeatures.costStars;
        out["action_affordable"] = actionFeatures.affordable;
        out["action_population_gain"] = actionFeatures.populationGain;
        out["observation"] = observation(std::nullopt, true, -1);
        return out;
    }

    py::tuple stepFast(size_t actionId, std::optional<int> rewardPlayer) {
        const NativeStepResult result = stepNative(actionId, rewardPlayer);
        return py::make_tuple(result.ok, result.done, result.reward, result.winner, result.currentPlayer);
    }

    py::tuple stepFastNoReveal(size_t actionId, std::optional<int> rewardPlayer) {
        ensureState();
        const PlayerId actor = session_->state.currentPlayer();
        const auto decoded = session_->state.decodeActionId(actor, actionId);
        if (!decoded) {
            return py::make_tuple(false, session_->state.isTerminal(), 0.0f, -1, static_cast<int>(session_->state.currentPlayer()));
        }
        const ActionModelFields actionFeatures = buildActionModelFields(session_->state.getGame(), *decoded);
        session_->apply(*decoded, actionId);
        // Keep mechanics/FOW updates from the core engine, but do not expose newly visible
        // tile content to the acting player's visible_only observation layers.
        revealObservedTilesForAllPlayersExcept(actor);
        invalidateCaches();

        const bool done = session_->state.isTerminal();
        const Game& g = session_->state.getGame();
        int rp = rewardPlayer.value_or(static_cast<int>(actor));
        float reward = 0.0f;
        const PlayerId terminalWinner = done ? terminalWinnerNative() : kNoPlayer;
        if (done && rp >= 0 && rp < static_cast<int>(g.getPlayers().size())) {
            reward = terminalValueForPlayer(static_cast<PlayerId>(rp));
        }
        const int winner = terminalWinner == kNoPlayer ? -1 : static_cast<int>(terminalWinner);
        return py::make_tuple(true, done, reward, winner, static_cast<int>(session_->state.currentPlayer()));
    }

    py::dict visibleEventsNumpy(uint64_t since) const {
        py::dict out;
        // The public Python API may read only the current player's stream.
        const size_t perspective = static_cast<size_t>(session_->state.currentPlayer());
        const PlayerEventJournal* journal = session_->events.stream(perspective);

        size_t first = 0;
        size_t eventCount = 0;
        size_t affectedCount = 0;
        uint64_t nextCursor = since;
        if (journal) {
            while (first < journal->events.size() && journal->events[first].sequence < since) ++first;
            eventCount = journal->events.size() - first;
            for (size_t i = first; i < journal->events.size(); ++i) {
                affectedCount += journal->events[i].affectedUnits.size();
            }
            nextCursor = journal->nextSequence;
        }

        auto sequence = makeArray1D<uint64_t>(eventCount);
        auto actionSequence = makeArray1D<uint64_t>(eventCount);
        auto round = makeArray1D<int32_t>(eventCount);
        auto turn = makeArray1D<int32_t>(eventCount);
        auto typeId = makeArray1D<int16_t>(eventCount);
        auto flags = makeArray1D<uint16_t>(eventCount);
        auto sourceIndex = makeArray1D<int32_t>(eventCount);
        auto targetIndex = makeArray1D<int32_t>(eventCount);
        auto damage = makeArray1D<int16_t>(eventCount);
        auto hpBefore = makeArray1D<int16_t>(eventCount);
        auto hpAfter = makeArray1D<int16_t>(eventCount);
        auto actorPlayer = makeArray1D<int16_t>(eventCount);
        auto actorTribe = makeArray1D<int16_t>(eventCount);
        auto tileActionKind = makeArray1D<int16_t>(eventCount);
        auto buildingType = makeArray1D<int16_t>(eventCount);
        auto spawnType = makeArray1D<int16_t>(eventCount);
        auto sourceUnitType = makeArray1D<int16_t>(eventCount);
        auto targetUnitType = makeArray1D<int16_t>(eventCount);
        auto sourceObservedUnitId = makeArray1D<int32_t>(eventCount);
        auto targetObservedUnitId = makeArray1D<int32_t>(eventCount);
        auto sourceUnitHpBefore = makeArray1D<int16_t>(eventCount);
        auto sourceUnitHpAfter = makeArray1D<int16_t>(eventCount);
        auto targetUnitHpBefore = makeArray1D<int16_t>(eventCount);
        auto targetUnitHpAfter = makeArray1D<int16_t>(eventCount);
        auto unitUpgradeKind = makeArray1D<int16_t>(eventCount);
        auto upgradedUnitType = makeArray1D<int16_t>(eventCount);
        auto unitDestroyed = makeArray1D<uint8_t>(eventCount);
        auto sourceUnitDestroyed = makeArray1D<uint8_t>(eventCount);
        auto affectedOffsets = makeArray1D<int64_t>(eventCount + 1);
        auto affectedObservedUnitId = makeArray1D<int32_t>(affectedCount);
        auto affectedTileIndex = makeArray1D<int32_t>(affectedCount);
        auto affectedUnitType = makeArray1D<int16_t>(affectedCount);
        auto affectedDamage = makeArray1D<int16_t>(affectedCount);
        auto affectedHpBefore = makeArray1D<int16_t>(affectedCount);
        auto affectedHpAfter = makeArray1D<int16_t>(affectedCount);
        auto affectedDestroyed = makeArray1D<uint8_t>(affectedCount);
        auto affectedSplash = makeArray1D<uint8_t>(affectedCount);
        affectedOffsets.mutable_unchecked<1>()(0) = 0;
        if (journal) {
            auto sequenceOut = sequence.mutable_unchecked<1>();
            auto actionSequenceOut = actionSequence.mutable_unchecked<1>();
            auto roundOut = round.mutable_unchecked<1>();
            auto turnOut = turn.mutable_unchecked<1>();
            auto typeOut = typeId.mutable_unchecked<1>();
            auto flagsOut = flags.mutable_unchecked<1>();
            auto sourceOut = sourceIndex.mutable_unchecked<1>();
            auto targetOut = targetIndex.mutable_unchecked<1>();
            auto damageOut = damage.mutable_unchecked<1>();
            auto hpBeforeOut = hpBefore.mutable_unchecked<1>();
            auto hpAfterOut = hpAfter.mutable_unchecked<1>();
            auto actorPlayerOut = actorPlayer.mutable_unchecked<1>();
            auto actorTribeOut = actorTribe.mutable_unchecked<1>();
            auto tileActionKindOut = tileActionKind.mutable_unchecked<1>();
            auto buildingTypeOut = buildingType.mutable_unchecked<1>();
            auto spawnTypeOut = spawnType.mutable_unchecked<1>();
            auto sourceUnitTypeOut = sourceUnitType.mutable_unchecked<1>();
            auto targetUnitTypeOut = targetUnitType.mutable_unchecked<1>();
            auto sourceObservedUnitIdOut = sourceObservedUnitId.mutable_unchecked<1>();
            auto targetObservedUnitIdOut = targetObservedUnitId.mutable_unchecked<1>();
            auto sourceUnitHpBeforeOut = sourceUnitHpBefore.mutable_unchecked<1>();
            auto sourceUnitHpAfterOut = sourceUnitHpAfter.mutable_unchecked<1>();
            auto targetUnitHpBeforeOut = targetUnitHpBefore.mutable_unchecked<1>();
            auto targetUnitHpAfterOut = targetUnitHpAfter.mutable_unchecked<1>();
            auto unitUpgradeKindOut = unitUpgradeKind.mutable_unchecked<1>();
            auto upgradedUnitTypeOut = upgradedUnitType.mutable_unchecked<1>();
            auto unitDestroyedOut = unitDestroyed.mutable_unchecked<1>();
            auto sourceUnitDestroyedOut = sourceUnitDestroyed.mutable_unchecked<1>();
            auto affectedOffsetsOut = affectedOffsets.mutable_unchecked<1>();
            auto affectedObservedUnitIdOut = affectedObservedUnitId.mutable_unchecked<1>();
            auto affectedTileIndexOut = affectedTileIndex.mutable_unchecked<1>();
            auto affectedUnitTypeOut = affectedUnitType.mutable_unchecked<1>();
            auto affectedDamageOut = affectedDamage.mutable_unchecked<1>();
            auto affectedHpBeforeOut = affectedHpBefore.mutable_unchecked<1>();
            auto affectedHpAfterOut = affectedHpAfter.mutable_unchecked<1>();
            auto affectedDestroyedOut = affectedDestroyed.mutable_unchecked<1>();
            auto affectedSplashOut = affectedSplash.mutable_unchecked<1>();
            size_t affectedOut = 0;
            affectedOffsetsOut(0) = 0;
            for (size_t i = 0; i < eventCount; ++i) {
                const ObservedEventRecord& event = journal->events[first + i];
                sequenceOut(i) = event.sequence;
                actionSequenceOut(i) = event.actionSequence;
                roundOut(i) = event.round;
                turnOut(i) = event.turn;
                typeOut(i) = event.typeId;
                flagsOut(i) = event.flags;
                sourceOut(i) = event.sourceIndex;
                targetOut(i) = event.targetIndex;
                damageOut(i) = event.damage;
                hpBeforeOut(i) = event.hpBefore;
                hpAfterOut(i) = event.hpAfter;
                actorPlayerOut(i) = event.actorPlayer;
                actorTribeOut(i) = event.actorTribe;
                tileActionKindOut(i) = event.tileActionKind;
                buildingTypeOut(i) = event.buildingType;
                spawnTypeOut(i) = event.spawnType;
                sourceUnitTypeOut(i) = event.sourceUnitType;
                targetUnitTypeOut(i) = event.targetUnitType;
                sourceObservedUnitIdOut(i) = event.sourceObservedUnitId;
                targetObservedUnitIdOut(i) = event.targetObservedUnitId;
                sourceUnitHpBeforeOut(i) = event.sourceUnitHpBefore;
                sourceUnitHpAfterOut(i) = event.sourceUnitHpAfter;
                targetUnitHpBeforeOut(i) = event.targetUnitHpBefore;
                targetUnitHpAfterOut(i) = event.targetUnitHpAfter;
                unitUpgradeKindOut(i) = event.unitUpgradeKind;
                upgradedUnitTypeOut(i) = event.upgradedUnitType;
                unitDestroyedOut(i) = event.unitDestroyed ? 1 : 0;
                sourceUnitDestroyedOut(i) = event.sourceUnitDestroyed ? 1 : 0;
                for (const auto& affected : event.affectedUnits) {
                    affectedObservedUnitIdOut(affectedOut) = affected.observedUnitId;
                    affectedTileIndexOut(affectedOut) = affected.tileIndex;
                    affectedUnitTypeOut(affectedOut) = affected.unitType;
                    affectedDamageOut(affectedOut) = affected.damage;
                    affectedHpBeforeOut(affectedOut) = affected.hpBefore;
                    affectedHpAfterOut(affectedOut) = affected.hpAfter;
                    affectedDestroyedOut(affectedOut) = affected.destroyed ? 1 : 0;
                    affectedSplashOut(affectedOut) = affected.splash ? 1 : 0;
                    ++affectedOut;
                }
                affectedOffsetsOut(i + 1) = static_cast<int64_t>(affectedOut);
            }
        }

        out["sequence"] = std::move(sequence);
        out["action_sequence"] = std::move(actionSequence);
        out["round"] = std::move(round);
        out["turn"] = std::move(turn);
        out["type_id"] = std::move(typeId);
        out["flags"] = std::move(flags);
        out["source_index"] = std::move(sourceIndex);
        out["target_index"] = std::move(targetIndex);
        out["damage"] = std::move(damage);
        out["hp_before"] = std::move(hpBefore);
        out["hp_after"] = std::move(hpAfter);
        out["actor_player"] = std::move(actorPlayer);
        out["actor_tribe"] = std::move(actorTribe);
        out["tile_action_kind"] = std::move(tileActionKind);
        out["building_type"] = std::move(buildingType);
        out["spawn_type"] = std::move(spawnType);
        out["source_unit_type"] = std::move(sourceUnitType);
        out["target_unit_type"] = std::move(targetUnitType);
        out["source_observed_unit_id"] = std::move(sourceObservedUnitId);
        out["target_observed_unit_id"] = std::move(targetObservedUnitId);
        out["source_unit_hp_before"] = std::move(sourceUnitHpBefore);
        out["source_unit_hp_after"] = std::move(sourceUnitHpAfter);
        out["target_unit_hp_before"] = std::move(targetUnitHpBefore);
        out["target_unit_hp_after"] = std::move(targetUnitHpAfter);
        out["unit_upgrade_kind"] = std::move(unitUpgradeKind);
        out["upgraded_unit_type"] = std::move(upgradedUnitType);
        out["unit_destroyed"] = std::move(unitDestroyed);
        out["source_unit_destroyed"] = std::move(sourceUnitDestroyed);
        out["affected_offsets"] = std::move(affectedOffsets);
        out["affected_observed_unit_id"] = std::move(affectedObservedUnitId);
        out["affected_tile_index"] = std::move(affectedTileIndex);
        out["affected_unit_type"] = std::move(affectedUnitType);
        out["affected_damage"] = std::move(affectedDamage);
        out["affected_hp_before"] = std::move(affectedHpBefore);
        out["affected_hp_after"] = std::move(affectedHpAfter);
        out["affected_destroyed"] = std::move(affectedDestroyed);
        out["affected_splash"] = std::move(affectedSplash);
        out["next_cursor"] = nextCursor;
        return out;
    }

    py::dict observation(std::optional<int> playerId,
                         bool visibleOnly,
                         int hiddenValue) const {
        ensureState();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int perspective = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));

        py::dict obs;
        obs["turn"] = g.getTurnNumber();
        obs["current_player"] = static_cast<int>(g.getCurrentPlayerId());
        obs["game_over"] = g.isGameOver();
        obs["winner"] = g.isGameOver() ? static_cast<int>(g.getWinner()) : -1;
        int perspectiveStars = -1;
        if (perspective >= 0 && static_cast<size_t>(perspective) < g.getPlayers().size()) {
            perspectiveStars = g.getPlayer(static_cast<PlayerId>(perspective)).getStars();
        }
        obs["player_stars"] = perspectiveStars;

        auto tokenized = tokenizedMap(playerId, visibleOnly, hiddenValue);
        int ownUnits = 0;
        std::unordered_set<int> ownCityIds;
        ownCityIds.reserve(16);
        for (const auto& tile : tokenized) {
            if (tile.size() < 13) continue;
            if (tile[3] == perspective) {
                ++ownUnits;
            }
            if (tile[14] != static_cast<int>(SettlementTypeEnum::City)) continue;
            const int cityIdRaw = tile[15];
            if (cityIdRaw < 0) continue;
            const CityId cid = static_cast<CityId>(cityIdRaw);
            const City* city = g.getCity(cid);
            if (!city) continue;
            if (static_cast<int>(city->getOwnerId()) == perspective) {
                ownCityIds.insert(cityIdRaw);
            }
        }
        obs["owns_units"] = ownUnits;
        obs["own_cities"] = static_cast<int>(ownCityIds.size());

        int nextTurnStarIncome = 0;
        if (perspective >= 0 && static_cast<size_t>(perspective) < g.getPlayers().size()) {
            const PlayerId pid = static_cast<PlayerId>(perspective);
            for (CityId cid : g.getPlayer(pid).getCities()) {
                if (!CitySystem::cityExists(g, cid)) continue;
                if (CitySystem::isCityUnderSiege(g, cid)) continue;
                if (CitySystem::getCityIsInfiltrated(g, cid)) continue;
                nextTurnStarIncome += static_cast<int>(CitySystem::getCityStarsPerRound(g, cid));
                nextTurnStarIncome += StarsSystem::marketIncomeForCity(g, pid, cid);
            }
        }
        obs["next_turn_star_income"] = std::max(0, nextTurnStarIncome);
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
        obs["map_type"] = mapType_ == MapType::Lakes ? "lakes" : "drylands";
        obs["tokenized_map"] = std::move(tokenized);
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
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int perspective = playerId.value_or(static_cast<int>(g.getCurrentPlayerId()));
        const std::vector<uint8_t>* knownObs = nullptr;
        if (visibleOnly && perspective >= 0) {
            const size_t perspectiveIdx = static_cast<size_t>(perspective);
            if (perspectiveIdx < session_->observations.knownByPlayer.size()) {
                knownObs = &session_->observations.knownByPlayer[perspectiveIdx];
            }
        }
        bool hasClimbing = false;
        bool hasOrganization = false;
        bool hasSailing = false;
        if (perspective >= 0 && perspective < static_cast<int>(g.getPlayers().size())) {
            const Player& p = g.getPlayer(static_cast<PlayerId>(perspective));
            hasClimbing = p.hasTech(TechId::Climbing);
            hasOrganization = p.hasTech(TechId::Organization);
            hasSailing = p.hasTech(TechId::Sailing);
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

        auto canRevealUnitOriginCity = [&](const Unit* u) -> bool {
            if (!u || u->getOriginCityId() == kNoCity) return false;
            if (!visibleOnly) return true;
            if (perspective < 0 || perspective >= 16) return false;
            if (static_cast<int>(u->getOwnerId()) == perspective) return true;

            const City* city = g.getCity(u->getOriginCityId());
            if (!city || !m.inBounds(city->getPos())) return false;
            const Pos cityPos = city->getPos();
            const uint16_t cityVisibility = static_cast<uint16_t>(m.at(cityPos).getVisibility());
            if ((cityVisibility & (uint16_t(1) << perspective)) == 0) return false;

            const int cityIndex = cityPos.y * w + cityPos.x;
            return !knownObs || (cityIndex >= 0 &&
                static_cast<size_t>(cityIndex) < knownObs->size() &&
                (*knownObs)[static_cast<size_t>(cityIndex)] != 0);
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
                int unitType = -1;
                int isCloakAround = 0;
                int capitalLayer = -1;
                //city level
                int cityLevel = -1;
                int ownUnitKills = -1;
                int unitMaxHp = -1;
                int unitOriginCity = -1;
                int resourceToken = static_cast<int>(t.getResource());
                int settlementTypeToken = static_cast<int>(t.getSettlementType());
                int settlementIdToken =
                    (static_cast<int>(t.getSettlementId()) == static_cast<int>(kNoSettlement))
                        ? -1
                        : static_cast<int>(t.getSettlementId());
                int cityOwnerToken = -1;
                int cityUnitsOccupiedToken = -1;
                int cityHasWorkshopToken = -1;
                int cityHasWallToken = -1;
                int cityParkCountToken = -1;

                if (!hasClimbing && resourceToken == static_cast<int>(ResourcesEnum::Metal)) {
                    resourceToken = static_cast<int>(ResourcesEnum::None);
                }
                if (!hasOrganization && resourceToken == static_cast<int>(ResourcesEnum::Crops)) {
                    resourceToken = static_cast<int>(ResourcesEnum::None);
                }
                if (!hasSailing && settlementTypeToken == static_cast<int>(SettlementTypeEnum::Starfish)) {
                    settlementTypeToken = static_cast<int>(SettlementTypeEnum::None);
                    settlementIdToken = -1;
                }
                if (t.getSettlementType() == SettlementTypeEnum::City) {
                    capitalLayer = 0;
                    const CityId cityId = static_cast<CityId>(t.getSettlementId());
                    if (cityId != kNoCity) {
                        const City* city = g.getCity(cityId);
                        if (city) {
                            cityLevel = static_cast<int>(city->getLevel());
                            const PlayerId owner = city->getOwnerId();
                            if (owner != kNoPlayer) {
                                cityOwnerToken = static_cast<int>(owner);
                            }
                            if (owner != kNoPlayer && static_cast<int>(owner) == perspective) {
                                cityUnitsOccupiedToken = static_cast<int>(CitySystem::getCityUnitsCount(g, cityId));
                            }
                            if (city->isCapitalCity()) {
                                capitalLayer = 1;
                            }
                            cityHasWorkshopToken = city->hasWorkshopEnabled() ? 1 : 0;
                            cityHasWallToken = city->hasCityWallEnabled() ? 1 : 0;
                            cityParkCountToken = static_cast<int>(city->getParkCount());
                        }
                    }
                }

                const UnitId uid = m.unitOn(p);
                if (uid != Map::kNoUnit) {
                    const Unit* u = g.getUnit(uid);
                    if (u) {
                        const bool hiddenEnemy = isEnemyHiddenUnitForPerspective(u);
                        if (!hiddenEnemy) {
                            unitHp = u->getHealth();
                            unitOwner = static_cast<int>(u->getOwnerId());
                            unitType = static_cast<int>(u->getType());
                            unitMaxHp = u->getMaxHealth();
                            if (canRevealUnitOriginCity(u)) {
                                unitOriginCity = static_cast<int>(u->getOriginCityId());
                            }
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
                    unitType,
                    ownUnitKills,
                    (static_cast<int>(t.getTerritoryCityId()) == static_cast<int>(kNoCity)) ? -1 : static_cast<int>(t.getTerritoryCityId()),
                    static_cast<int>(t.getRoadBridge()),
                    static_cast<int>(t.getBuildingType()),
                    capitalLayer,
                    cityHasWorkshopToken,
                    cityHasWallToken,
                    cityParkCountToken,
                    cityLevel,
                    settlementTypeToken,
                    settlementIdToken,
                    cityOwnerToken,
                    cityUnitsOccupiedToken,
                    resourceToken,
                    static_cast<int>(t.getBaseTerrain()),
                    static_cast<int>(t.getTribe()),
                    unitMaxHp,
                    unitOriginCity,
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

    std::vector<std::vector<int>> playerMap(std::optional<int> playerId = std::nullopt,
                                            int hiddenValue = -1) const {
        return tokenizedMap(playerId, true, hiddenValue);
    }

    py::array_t<int32_t> playerMapNumpy(std::optional<int> playerId = std::nullopt,
                                        int hiddenValue = -1) const {
        return mapRowsToNumpy(playerMap(playerId, hiddenValue));
    }

    // Native training encoder. Unlike modelRequestNumpy(), this writes directly
    // to caller-owned dense buffers: no JSON, Python dicts, strings or
    // per-tile vectors are created on the hot path.
    void writeTrainingPacket(
        int32_t* mapTokensOut,
        int32_t* stateOut,
        int32_t* actionIdsOut,
        int32_t* actionFeaturesOut,
        uint8_t* actionArgMasksOut,
        uint8_t* actionMaskOut,
        int32_t* legalActionCountOut,
        int32_t* totalLegalActionCountOut,
        uint8_t* actionTruncatedOut,
        size_t maxActions,
        bool includeCombatPreview) const
    {
        writePlayerMapTokens(mapTokensOut);
        writeTrainingState(stateOut);

        const Game& g = session_->state.getGame();
        const PlayerId pid = session_->state.currentPlayer();
        const ModelActionWindow actions = modelActionWindow(maxActions);
        *legalActionCountOut = static_cast<int32_t>(actions.count);
        *totalLegalActionCountOut = static_cast<int32_t>(actions.totalCount);
        *actionTruncatedOut = actions.truncated() ? 1 : 0;

        const size_t count = actions.count;
        for (size_t row = 0; row < count; ++row) {
            const size_t actionId = actions.actionId(row);
            const auto action = session_->state.decodeActionId(pid, actionId);
            if (!action) continue;

            const ActionModelFields fields = buildActionModelFields(g, *action);
            int damageDealt = -1;
            int damageReceived = -1;
            if (includeCombatPreview && action->type == Action::Type::Attack) {
                std::tie(damageDealt, damageReceived) = predictAttackDamage(g, *action);
            }

            actionIdsOut[row] = static_cast<int32_t>(actionId);
            actionMaskOut[row] = 1;
            int32_t* dst = actionFeaturesOut + row * kVectorActionFeatureCount;
            dst[0] = fields.typeId;
            dst[1] = fields.sourceIndex;
            dst[2] = fields.targetIndex;
            dst[3] = fields.city;
            dst[4] = fields.tech;
            dst[5] = fields.building;
            dst[6] = fields.spawnType;
            dst[7] = fields.upgrade;
            dst[8] = fields.tileAction;
            dst[9] = fields.unitUpgrade;
            dst[10] = fields.unitId;
            dst[11] = fields.populationGain;
            dst[12] = fields.costStars;
            dst[13] = fields.starsBefore;
            dst[14] = fields.affordable;
            dst[15] = damageDealt;
            dst[16] = damageReceived;
            writeActionArgMask(*action, actionArgMasksOut + row * kVectorActionArgMaskCount);
        }
    }

    // Encodes the suffix of the *current player's* private observed-event
    // stream. The fixed layout is intentional: it lets vectorized policies
    // transfer a complete history window to a GPU without ragged Python data.
    // Valid records are right-aligned so the newest event is always at
    // historyLength - 1. All padding is marked by eventMaskOut == 0.
    void writeVisibleEventHistory(
        int32_t* eventFeaturesOut,
        uint64_t* sequenceOut,
        uint64_t* actionSequenceOut,
        uint8_t* eventMaskOut,
        int32_t* affectedOut,
        uint8_t* affectedMaskOut,
        size_t historyLength) const
    {
        if (historyLength == 0) return;
        ensureState();
        const size_t perspective = static_cast<size_t>(session_->state.currentPlayer());
        const PlayerEventJournal* journal = session_->events.stream(perspective);
        if (!journal || journal->events.empty()) return;

        const size_t eventCount = std::min(historyLength, journal->events.size());
        const size_t journalStart = journal->events.size() - eventCount;
        const size_t outputStart = historyLength - eventCount;
        for (size_t item = 0; item < eventCount; ++item) {
            const ObservedEventRecord& event = journal->events[journalStart + item];
            if (event.affectedUnits.size() > kVisibleEventAffectedUnitCount) {
                throw std::runtime_error(
                    "visible event has more affected units than the fixed vector batch layout supports");
            }

            const size_t row = outputStart + item;
            int32_t* fields = eventFeaturesOut + row * kVisibleEventFeatureCount;
            fields[0] = event.round;
            fields[1] = event.turn;
            fields[2] = event.typeId;
            fields[3] = event.flags;
            fields[4] = event.sourceIndex;
            fields[5] = event.targetIndex;
            fields[6] = event.damage;
            fields[7] = event.hpBefore;
            fields[8] = event.hpAfter;
            fields[9] = event.actorPlayer;
            fields[10] = event.actorTribe;
            fields[11] = event.tileActionKind;
            fields[12] = event.buildingType;
            fields[13] = event.spawnType;
            fields[14] = event.sourceUnitType;
            fields[15] = event.targetUnitType;
            fields[16] = event.sourceObservedUnitId;
            fields[17] = event.targetObservedUnitId;
            fields[18] = event.sourceUnitHpBefore;
            fields[19] = event.sourceUnitHpAfter;
            fields[20] = event.targetUnitHpBefore;
            fields[21] = event.targetUnitHpAfter;
            fields[22] = event.unitUpgradeKind;
            fields[23] = event.upgradedUnitType;
            fields[24] = event.unitDestroyed ? 1 : 0;
            fields[25] = event.sourceUnitDestroyed ? 1 : 0;
            sequenceOut[row] = event.sequence;
            actionSequenceOut[row] = event.actionSequence;
            eventMaskOut[row] = 1;

            for (size_t affectedIndex = 0; affectedIndex < event.affectedUnits.size(); ++affectedIndex) {
                const ObservedEventRecord::AffectedUnit& affected = event.affectedUnits[affectedIndex];
                int32_t* affectedFields = affectedOut +
                    (row * kVisibleEventAffectedUnitCount + affectedIndex) *
                    kVisibleEventAffectedFeatureCount;
                affectedFields[0] = affected.observedUnitId;
                affectedFields[1] = affected.tileIndex;
                affectedFields[2] = affected.unitType;
                affectedFields[3] = affected.damage;
                affectedFields[4] = affected.hpBefore;
                affectedFields[5] = affected.hpAfter;
                affectedFields[6] = affected.destroyed ? 1 : 0;
                affectedFields[7] = affected.splash ? 1 : 0;
                affectedMaskOut[row * kVisibleEventAffectedUnitCount + affectedIndex] = 1;
            }
        }
    }

    // This is only used by VectorGameEnv. A scalar GameEnv keeps its complete
    // journal because its cursor-based visible_events_numpy() API promises it.
    void retainVisibleEventHistory(size_t maxEventsPerPlayer) {
        ensureState();
        session_->events.retainLast(maxEventsPerPlayer);
    }

    std::vector<std::vector<int>> fullMap() const {
        ensureState();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();

        std::vector<std::vector<int>> out;
        out.reserve(static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h)));

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const Tile& t = m.at(p);

                int unitHp = -1;
                int unitOwner = -1;
                int unitType = -1;
                int ownUnitKills = -1;
                int unitMaxHp = -1;
                int unitOriginCity = -1;
                const UnitId uid = m.unitOn(p);
                if (uid != Map::kNoUnit) {
                    if (const Unit* u = g.getUnit(uid)) {
                        unitHp = u->getHealth();
                        unitOwner = static_cast<int>(u->getOwnerId());
                        unitType = static_cast<int>(u->getType());
                        unitMaxHp = u->getMaxHealth();
                        if (u->getOriginCityId() != kNoCity) {
                            unitOriginCity = static_cast<int>(u->getOriginCityId());
                        }
                        ownUnitKills = u->getKillCounter();
                    }
                }

                int capitalLayer = -1;
                int cityLevel = -1;
                int cityOwnerToken = -1;
                int cityUnitsOccupiedToken = -1;
                int cityHasWorkshopToken = -1;
                int cityHasWallToken = -1;
                int cityParkCountToken = -1;
                if (t.getSettlementType() == SettlementTypeEnum::City) {
                    capitalLayer = 0;
                    const CityId cityId = static_cast<CityId>(t.getSettlementId());
                    if (cityId != kNoCity) {
                        if (const City* city = g.getCity(cityId)) {
                            cityLevel = static_cast<int>(city->getLevel());
                            const PlayerId owner = city->getOwnerId();
                            if (owner != kNoPlayer) {
                                cityOwnerToken = static_cast<int>(owner);
                                cityUnitsOccupiedToken = static_cast<int>(CitySystem::getCityUnitsCount(g, cityId));
                            }
                            if (city->isCapitalCity()) {
                                capitalLayer = 1;
                            }
                            cityHasWorkshopToken = city->hasWorkshopEnabled() ? 1 : 0;
                            cityHasWallToken = city->hasCityWallEnabled() ? 1 : 0;
                            cityParkCountToken = static_cast<int>(city->getParkCount());
                        }
                    }
                }

                out.push_back(std::vector<int>{
                    1,
                    0,
                    unitHp,
                    unitOwner,
                    unitType,
                    ownUnitKills,
                    (static_cast<int>(t.getTerritoryCityId()) == static_cast<int>(kNoCity)) ? -1 : static_cast<int>(t.getTerritoryCityId()),
                    static_cast<int>(t.getRoadBridge()),
                    static_cast<int>(t.getBuildingType()),
                    capitalLayer,
                    cityHasWorkshopToken,
                    cityHasWallToken,
                    cityParkCountToken,
                    cityLevel,
                    static_cast<int>(t.getSettlementType()),
                    (static_cast<int>(t.getSettlementId()) == static_cast<int>(kNoSettlement)) ? -1 : static_cast<int>(t.getSettlementId()),
                    cityOwnerToken,
                    cityUnitsOccupiedToken,
                    static_cast<int>(t.getResource()),
                    static_cast<int>(t.getBaseTerrain()),
                    static_cast<int>(t.getTribe()),
                    unitMaxHp,
                    unitOriginCity,
                });
            }
        }

        return out;
    }

    py::array_t<int32_t> fullMapNumpy() const {
        return mapRowsToNumpy(fullMap());
    }

    std::vector<int> lighthouseDiscoveredByMasks() const {
        ensureState();
        const Game& g = session_->state.getGame();
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
        const Game& g = session_->state.getGame();
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
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        py::dict spec;
        spec["type_vocab_size"] = kActionTypeCount;
        spec["type_fullname_format"] = "base_type or base_type:subtype";
        spec["type_fullname_examples"] = std::vector<std::string>{
            "move", "attack", "heal", "end_turn", "buy_tech:fishing", "upgrade_city:super_unit", "build:farm", "spawn_unit:warrior",
            "tile_action:hunt", "unit_upgrade:raft_to_scout"};
        spec["tile_vocab_size"] = m.getWidth() * m.getHeight();
        spec["tech_vocab_size"] = kTechVocabSize;
        spec["building_vocab_size"] = kBuildingVocabSize;
        spec["spawn_type_vocab_size"] = kSpawnTypeVocabSize;
        spec["city_upgrade_vocab_size"] = kCityUpgradeVocabSize;
        spec["tile_action_vocab_size"] = kTileActionVocabSize;
        spec["unit_upgrade_vocab_size"] = kUnitUpgradeVocabSize;
        spec["population_gain_vocab_size"] = kPopulationGainVocabSize;
        spec["unit_id_vocab_size"] = static_cast<int>(g.getUnits().size());
        spec["tech_none_id"] = static_cast<int>(TechId::Count);
        spec["source_none_id"] = -1;
        spec["target_none_id"] = -1;
        spec["city_none_id"] = -1;
        spec["unit_id_none_id"] = -1;
        spec["map_width"] = m.getWidth();
        spec["map_height"] = m.getHeight();
        spec["arg_order"] = std::vector<std::string>{
            "source_index", "target_index", "tech", "building", "spawn_type", "upgrade", "tile_action", "unit_upgrade",
            "unit_id", "damage_dealt", "damage_received", "population_gain"};
        return spec;
    }

    std::vector<py::dict> legalParamActions() const {
        ensureState();
        ensureLegalIdsCache();
        const Game& g = session_->state.getGame();
        std::vector<py::dict> out;
        out.reserve(legalIdsCache_.size());
        const int currentStars = g.getPlayer(g.getCurrentPlayerId()).getStars();
        for (size_t aid : legalIdsCache_) {
            const auto a = session_->state.decodeActionId(session_->state.currentPlayer(), aid);
            if (!a) continue;
            const ActionModelFields f = buildActionModelFields(g, *a);
            py::dict d;
            d["action_id"] = aid;
            d["type"] = actionBaseTypeName(a->type);
            d["type_id"] = f.typeId;
            d["type_fullname"] = actionFullTypeName(*a);
            d["source_index"] = f.sourceIndex;
            d["target_index"] = f.targetIndex;
            d["city"] = f.city;
            d["tech"] = f.tech;
            d["building"] = f.building;
            d["spawn_type"] = f.spawnType;
            d["upgrade"] = f.upgrade;
            d["tile_action"] = f.tileAction;
            d["unit_upgrade"] = f.unitUpgrade;
            d["unit_id"] = f.unitId;
            d["unit"] = f.unitId;
            d["population_gain"] = f.populationGain;
            d["cost_stars"] = f.costStars;
            d["stars_before"] = currentStars;
            d["affordable"] = (f.costStars <= currentStars) ? 1 : 0;
            const auto [damageDealt, damageReceived] = predictAttackDamage(g, *a);
            d["damage_dealt"] = damageDealt;
            d["damage_received"] = damageReceived;
            d["arg_mask"] = actionArgMaskForType(*a);
            out.push_back(std::move(d));
        }
        return out;
    }

    std::vector<py::dict> getActions() const {
        return legalParamActions();
    }

    py::dict modelRequest() {
        ensureState();
        json req = GameStateSerializer::buildRequest(session_->state.getGame(), session_->state);

        py::dict out;
        out["map_tokens"] = playerMap(std::nullopt, -1);
        out["obs"] = observation(std::nullopt, true, -1);
        out["actions"] = jsonToPy(req["actions"]);
        out["spec"] = jsonToPy(req["spec"]);
        return out;
    }

    py::dict requestPacket() {
        return modelRequest();
    }

    py::dict modelRequestNumpy() {
        ensureState();
        json req = GameStateSerializer::buildRequest(session_->state.getGame(), session_->state);

        py::dict out;
        out["map_tokens"] = playerMapNumpy(std::nullopt, -1);
        // model_request_numpy() is the hot training path.  The tokenized map
        // is already exposed above as a contiguous NumPy array, so do not
        // retain the legacy Python-list copy in obs.
        py::dict obs = observation(std::nullopt, true, -1);
        obs.attr("pop")("tokenized_map");
        out["obs"] = std::move(obs);
        out["actions"] = actionsToNumpy(req["actions"]);
        out["spec"] = jsonToPy(req["spec"]);
        return out;
    }

    py::dict requestPacketNumpy() {
        return modelRequestNumpy();
    }

    py::dict legalParamMasks() const {
        ensureState();
        ensureLegalIdsCache();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int tileCount = m.getWidth() * m.getHeight();
        const int unitIdVocabSize = static_cast<int>(g.getUnits().size());

        std::vector<uint8_t> typeMask(kActionTypeCount, 0);
        std::vector<uint8_t> sourceMask(std::max(0, tileCount), 0);
        std::vector<uint8_t> targetMask(std::max(0, tileCount), 0);
        std::vector<uint8_t> techMask(kTechVocabSize, 0);
        std::vector<uint8_t> buildingMask(kBuildingVocabSize, 0);
        std::vector<uint8_t> spawnTypeMask(kSpawnTypeVocabSize, 0);
        std::vector<uint8_t> upgradeMask(kCityUpgradeVocabSize, 0);
        std::vector<uint8_t> tileActionMask(kTileActionVocabSize, 0);
        std::vector<uint8_t> unitUpgradeMask(kUnitUpgradeVocabSize, 0);
        std::vector<uint8_t> populationGainMask(kPopulationGainVocabSize, 0);
        std::vector<uint8_t> unitIdMask(std::max(0, unitIdVocabSize), 0);

        std::vector<std::vector<uint8_t>> sourceMaskByType(kActionTypeCount, std::vector<uint8_t>(std::max(0, tileCount), 0));
        std::vector<std::vector<uint8_t>> targetMaskByType(kActionTypeCount, std::vector<uint8_t>(std::max(0, tileCount), 0));
        std::vector<std::vector<uint8_t>> techMaskByType(kActionTypeCount, std::vector<uint8_t>(kTechVocabSize, 0));
        std::vector<std::vector<uint8_t>> buildingMaskByType(kActionTypeCount, std::vector<uint8_t>(kBuildingVocabSize, 0));
        std::vector<std::vector<uint8_t>> spawnTypeMaskByType(kActionTypeCount, std::vector<uint8_t>(kSpawnTypeVocabSize, 0));
        std::vector<std::vector<uint8_t>> upgradeMaskByType(kActionTypeCount, std::vector<uint8_t>(kCityUpgradeVocabSize, 0));
        std::vector<std::vector<uint8_t>> tileActionMaskByType(kActionTypeCount, std::vector<uint8_t>(kTileActionVocabSize, 0));
        std::vector<std::vector<uint8_t>> unitUpgradeMaskByType(kActionTypeCount, std::vector<uint8_t>(kUnitUpgradeVocabSize, 0));
        std::vector<std::vector<uint8_t>> populationGainMaskByType(kActionTypeCount, std::vector<uint8_t>(kPopulationGainVocabSize, 0));
        std::vector<std::vector<uint8_t>> unitIdMaskByType(kActionTypeCount, std::vector<uint8_t>(std::max(0, unitIdVocabSize), 0));
        std::unordered_map<long long, std::vector<uint8_t>> targetMaskByTypeSource;

        const int currentStars = g.getPlayer(g.getCurrentPlayerId()).getStars();
        std::vector<int> actionCostById(session_->state.actionSpace().size(), -1);
        std::vector<uint8_t> affordableMask(session_->state.actionSpace().size(), 0);

        for (size_t aid : legalIdsCache_) {
            const auto a = session_->state.decodeActionId(session_->state.currentPlayer(), aid);
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
            {
                const int pg = std::clamp(f.populationGain, 0, kPopulationGainVocabSize - 1);
                populationGainMask[static_cast<size_t>(pg)] = 1;
                populationGainMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(pg)] = 1;
            }
            if (f.unitId >= 0 && f.unitId < unitIdVocabSize) {
                unitIdMask[static_cast<size_t>(f.unitId)] = 1;
                unitIdMaskByType[static_cast<size_t>(f.typeId)][static_cast<size_t>(f.unitId)] = 1;
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
        py::dict populationGainByType;
        py::dict unitIdByType;
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
            populationGainByType[py::int_(t)] = populationGainMaskByType[static_cast<size_t>(t)];
            unitIdByType[py::int_(t)] = unitIdMaskByType[static_cast<size_t>(t)];
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
        out["population_gain_mask"] = std::move(populationGainMask);
        out["unit_id_mask"] = std::move(unitIdMask);
        out["source_mask_by_type"] = std::move(sourceByType);
        out["target_mask_by_type"] = std::move(targetByType);
        out["target_mask_by_type_source"] = std::move(targetByTypeSource);
        out["tech_mask_by_type"] = std::move(techByType);
        out["building_mask_by_type"] = std::move(buildingByType);
        out["spawn_type_mask_by_type"] = std::move(spawnByType);
        out["upgrade_mask_by_type"] = std::move(upgradeByType);
        out["tile_action_mask_by_type"] = std::move(tileActionByType);
        out["unit_upgrade_mask_by_type"] = std::move(unitUpgradeByType);
        out["population_gain_mask_by_type"] = std::move(populationGainByType);
        out["unit_id_mask_by_type"] = std::move(unitIdByType);
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
            out["done"] = session_->state.isTerminal();
            out["reward"] = 0.0f;
            out["selected_action_id"] = -1;
            out["observation"] = observation(std::nullopt, true, -1);
            out["error"] = "Missing required field: type_id";
            return out;
        }

        const int wantedType = py::cast<int>(param["type_id"]);
        auto has = [&param](const char* key) { return param.contains(key); };
        auto getInt = [&param](const char* key) { return py::cast<int>(param[key]); };

        int selected = -1;
        const Game& g = session_->state.getGame();
        for (size_t aid : legalIdsCache_) {
            const auto a = session_->state.decodeActionId(session_->state.currentPlayer(), aid);
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
            if (has("unit_id") && f.unitId != getInt("unit_id")) continue;
            if (has("population_gain") && f.populationGain != getInt("population_gain")) continue;
            if (has("city") && f.city != getInt("city")) continue;
            selected = static_cast<int>(aid);
            break;
        }

        if (selected < 0) {
            py::dict out;
            out["ok"] = false;
            out["done"] = session_->state.isTerminal();
            out["reward"] = 0.0f;
            out["selected_action_id"] = -1;
            out["observation"] = observation(std::nullopt, true, -1);
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
            out["observation"] = observation(std::nullopt, true, -1);
            out["error"] = "Vector must contain at least type_id.";
            return out;
        }

        // [type_id, source_index, target_index, tech, building, spawn_type, upgrade, tile_action, unit_upgrade, unit_id, population_gain, city]
        py::dict param;
        param["type_id"] = vec[0];
        const std::vector<std::string> keys = {
            "source_index", "target_index", "tech", "building", "spawn_type",
            "upgrade", "tile_action", "unit_upgrade", "unit_id", "population_gain", "city"};
        for (size_t i = 1; i < vec.size() && i <= keys.size(); ++i) {
            if (vec[i] < 0) continue;
            param[py::str(keys[i - 1])] = vec[i];
        }
        return stepParam(param, rewardPlayer);
    }

    py::dict decodeAction(size_t actionId) const {
        ensureState();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const auto a = session_->state.decodeActionId(session_->state.currentPlayer(), actionId);
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
        out["type"] = actionBaseTypeName(a->type);
        out["type_id"] = f.typeId;
        out["type_fullname"] = actionFullTypeName(*a);
        out["pid"] = static_cast<int>(a->pid);
        out["unit"] = f.unitId;
        out["unit_id"] = f.unitId;
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
        out["population_gain"] = f.populationGain;
        out["arg_mask"] = actionArgMaskForType(*a);
        return out;
    }

    size_t actionSpaceSize() const {
        ensureState();
        return session_->state.actionSpace().size();
    }

    int currentPlayer() const {
        ensureState();
        return static_cast<int>(session_->state.currentPlayer());
    }

    uint32_t worldSeed() const {
        ensureState();
        return session_->state.getGame().getWorldSeed();
    }

    std::vector<int> playerTribes() const {
        ensureState();
        const Game& g = session_->state.getGame();
        std::vector<int> out;
        out.reserve(g.getPlayers().size());
        for (const Player& player : g.getPlayers()) {
            out.push_back(static_cast<int>(player.getTribeType()));
        }
        return out;
    }

    std::vector<size_t> replayActionIds() const {
        return session_->replay.actionIds();
    }

    void saveReplay(const std::string& path) const {
        ensureState();
        const Game& game = session_->state.getGame();
        ReplayMetadata metadata;
        metadata.seed = game.getWorldSeed();
        metadata.mapSize = game.getMap().getWidth();
        metadata.mapType = static_cast<uint8_t>(mapType_);
        metadata.ruleset = "polyenv-2026-07";
        for (const Player& player : game.getPlayers()) {
            metadata.tribes.push_back(static_cast<int>(player.getTribeType()));
        }
        std::string error;
        if (!ReplayRecorder::save(path, metadata, session_->replay.actionIds(), error)) {
            throw std::runtime_error("Could not save .polygame: " + error);
        }
    }

    py::dict loadReplay(const std::string& path) {
        ReplayMetadata metadata;
        std::vector<size_t> actionIds;
        std::string error;
        if (!ReplayRecorder::load(path, metadata, actionIds, error)) {
            throw std::runtime_error("Could not load .polygame: " + error);
        }

        GameEnv replayed(metadata.mapSize, metadata.tribes, metadata.seed, unitsJsonPath_,
                         static_cast<MapType>(metadata.mapType));
        replayed.reset(std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
        for (size_t index = 0; index < actionIds.size(); ++index) {
            const py::tuple result = replayed.stepFast(actionIds[index], std::nullopt);
            if (!py::cast<bool>(result[0])) {
                throw std::runtime_error(
                    "Invalid .polygame: action at index " + std::to_string(index) + " is not legal"
                );
            }
        }

        *this = replayed;
        return observation(std::nullopt, true, -1);
    }

    std::vector<int> getTechs(std::optional<int> playerId) const {
        ensureState();
        const Game& g = session_->state.getGame();
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
        const Game& g = session_->state.getGame();
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

    // Creates a detached rollout world from a complete token hypothesis.  This
    // deliberately does not call clone(): hidden source-world state is never
    // copied into the returned environment.
    GameEnv makeBeliefEnv(py::object completedMapTokens,
                          std::optional<int> perspectiveOpt = std::nullopt) const {
        ensureState();
        ensureObservationKnowledgeLayout();
        const Game& source = session_->state.getGame();
        const int perspectiveInt = perspectiveOpt.value_or(static_cast<int>(source.getCurrentPlayerId()));
        if (perspectiveInt < 0 || static_cast<size_t>(perspectiveInt) >= source.getPlayers().size()) {
            throw std::invalid_argument("perspective must identify a player in this game");
        }
        const PlayerId perspective = static_cast<PlayerId>(perspectiveInt);
        const auto tokens = completedMapTokensFromPy(completedMapTokens);
        const auto observed = tokenizedMap(perspectiveInt, true, -1);
        if (tokens.size() != observed.size()) {
            throw std::invalid_argument("completed_map_tokens has an invalid tile count");
        }
        for (size_t tile = 0; tile < tokens.size(); ++tile) {
            if (tokens[tile].size() != 23) {
                throw std::invalid_argument("each completed_map_tokens row must have exactly 23 values");
            }
            if (observed[tile].empty() || tokens[tile][0] != observed[tile][0]) {
                throw std::invalid_argument("completed_map_tokens must preserve the source visibility mask");
            }
            if (observed[tile][0] != 1) continue;
            if (tokens[tile] != observed[tile]) {
                throw std::invalid_argument(
                    "revealed tiles differ: completed_map_tokens disagrees with the current observation at tile "
                    + std::to_string(tile));
            }
        }

        auto beliefSession = std::make_shared<GameSession>(
            BeliefWorldBuilder::build(source, perspective, tokens));
        GameEnv result(std::move(beliefSession), source.getMap().getWidth(), tribesRaw_, seed_, unitsJsonPath_, mapType_);
        result.initializeObservationKnowledgeFromCurrentVisibility();
        return result;
    }

    // Batched self-play uses this path after it has already validated a
    // contiguous int32 NumPy tensor. The caller never receives a full source
    // map: this compares the supplied hypothesis with the current player's
    // observation in native memory, then asks BeliefWorldBuilder to construct
    // an isolated world directly from the flat tensor.
    GameEnv makeBeliefEnvFromCurrentPlayerFlatTokens(const int32_t* tokens,
                                                     size_t rowCount,
                                                     size_t columnCount,
                                                     int32_t* observedScratch) const {
        ensureState();
        if (columnCount != kMapTokenFeatureCount) {
            throw std::invalid_argument("completed_map_tokens must have exactly 23 columns");
        }
        const size_t expectedTiles = static_cast<size_t>(mapSize_) * static_cast<size_t>(mapSize_);
        if (rowCount != expectedTiles) {
            throw std::invalid_argument("completed_map_tokens has an invalid tile count");
        }
        if (!tokens && rowCount != 0) {
            throw std::invalid_argument("completed_map_tokens data is null");
        }

        if (!observedScratch) {
            throw std::invalid_argument("belief observation scratch buffer is null");
        }
        writePlayerMapTokens(observedScratch);
        for (size_t tile = 0; tile < expectedTiles; ++tile) {
            const int32_t* candidate = tokens + tile * columnCount;
            const int32_t* visible = observedScratch + tile * kMapTokenFeatureCount;
            if (candidate[0] != visible[0]) {
                throw std::invalid_argument("completed_map_tokens must preserve the source visibility mask");
            }
            if (visible[0] == 0) continue;
            for (size_t column = 0; column < kMapTokenFeatureCount; ++column) {
                if (candidate[column] != visible[column]) {
                    throw std::invalid_argument(
                        "revealed tiles differ: completed_map_tokens disagrees with the current observation at tile "
                        + std::to_string(tile));
                }
            }
        }

        const Game& source = session_->state.getGame();
        const PlayerId perspective = session_->state.currentPlayer();
        auto beliefSession = std::make_shared<GameSession>(
            BeliefWorldBuilder::buildFlat(source, perspective, tokens, rowCount, columnCount));
        GameEnv result(std::move(beliefSession), source.getMap().getWidth(), tribesRaw_, seed_, unitsJsonPath_, mapType_);
        result.initializeObservationKnowledgeFromCurrentVisibility();
        return result;
    }

    bool hasSameCurrentLegalActions(const GameEnv& other) const {
        ensureState();
        other.ensureState();
        if (session_->state.currentPlayer() != other.session_->state.currentPlayer()) return false;
        return session_->state.legalActionIdsRef(session_->state.currentPlayer()) ==
            other.session_->state.legalActionIdsRef(other.session_->state.currentPlayer());
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
        return session_->state.isTerminal();
    }

    // Returns tile indices revealed by the most recent step for the given player.
    // The engine updates this automatically during revealObservedTilesForPlayer().
    // For step_fast_no_reveal the acting player's list is always empty (no leak).
    std::vector<int> lastRevealedIndices(
        std::optional<int> perspectiveOpt = std::nullopt) const
    {
        ensureState();
        const int perspective = perspectiveOpt.value_or(
            static_cast<int>(session_->state.getGame().getCurrentPlayerId()));
        if (perspective < 0 ||
            static_cast<size_t>(perspective) >= session_->observations.lastRevealedByPlayer.size())
            return {};
        return session_->observations.lastRevealedByPlayer[static_cast<size_t>(perspective)];
    }

    // Returns tile indices that are hidden (not yet observed) for the given perspective.
    std::vector<int> hiddenTileIndices(std::optional<int> perspectiveOpt = std::nullopt) const {
        ensureState();
        ensureObservationKnowledgeLayout();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int tileCount = m.getWidth() * m.getHeight();
        const int perspective = perspectiveOpt.value_or(
            static_cast<int>(g.getCurrentPlayerId()));
        std::vector<int> result;
        if (perspective < 0 ||
            static_cast<size_t>(perspective) >= session_->observations.knownByPlayer.size())
            return result;
        const auto& known = session_->observations.knownByPlayer[static_cast<size_t>(perspective)];
        result.reserve(static_cast<size_t>(tileCount) / 2);
        for (int i = 0; i < tileCount; ++i) {
            if (!known[static_cast<size_t>(i)]) result.push_back(i);
        }
        return result;
    }

    // Apply predicted tile features for hidden tiles only.
    //
    // Accepts a sparse dict {tile_index → feature_vector_of_23_ints} produced by
    // the tile-prediction model (same layout as tokenized_map()).
    // Tiles already visible to `perspective` are silently skipped (guard in C++).
    //
    // Features written (safe to patch directly on Tile):
    //   [7]  roadBridge      : None=0, Road=1, Bridge=2, WaterConnection=3
    //   [8]  buildingType    : None=0, Farm=1..Windmill=8, monuments=9-15, Lighthouse=16
    //   [14] settlementType  : None=0, Village=1, Starfish=3, Ruin=4
    //                          City=2 is intentionally skipped (requires a Game-level City object)
    //   [18] resource        : None=0 … Metal=6
    //   [19] baseTerrain     : Ocean=0 … Forest=4
    //   [20] tribe           : Unknown=0, XinXi=1 … Cymanti=16
    //
    // Features NOT written (complex game-level state):
    //   [0]  visibility, [1] isCloakAround, [2-5] unit state,
    //   [6]  territoryCityId, [9-10] capitalLayer/cityLevel,
    //   [10-12] city workshop/wall/park state, [15-17] settlement/city state,
    //   [21] unitMaxHp, [22] unitOriginCityId
    //
    // Returns the number of tiles actually patched.
    int applyTilePredictions(
        const std::unordered_map<int, std::vector<int>>& predictions,
        std::optional<int> perspectiveOpt = std::nullopt)
    {
        ensureState();
        ensureObservationKnowledgeLayout();
        Game& g = session_->state.getGame();
        Map& m = g.getMap();
        const int tileCount = m.getWidth() * m.getHeight();
        const int perspective = perspectiveOpt.value_or(
            static_cast<int>(g.getCurrentPlayerId()));

        const std::vector<uint8_t>* knownObs = nullptr;
        if (perspective >= 0 &&
            static_cast<size_t>(perspective) < session_->observations.knownByPlayer.size())
            knownObs = &session_->observations.knownByPlayer[static_cast<size_t>(perspective)];

        int patched = 0;
        for (const auto& [idx, features] : predictions) {
            if (idx < 0 || idx >= tileCount) continue;
            if (static_cast<int>(features.size()) < 23) continue;
            // Guard: silently skip tiles already visible to this perspective.
            if (knownObs && (*knownObs)[static_cast<size_t>(idx)]) continue;

            Tile& tile = m.at(Pos{idx % m.getWidth(), idx / m.getWidth()});

            // [7] roadBridge
            { const int v = features[7];
              if (v >= 0 && v <= 3)
                  tile.setRoadBridge(static_cast<RoadBridgeEnum>(v)); }
            // [8] buildingType
            { const int v = features[8];
              if (v >= 0 && v <= 16)
                  tile.setBuildingType(static_cast<BuildingTypeEnum>(v)); }
            // [14] settlementType — City=2 intentionally skipped
            { const int v = features[14];
              if (v == 0) {
                  tile.clearSettlement();
              } else if (v == 1 || v == 3 || v == 4) {
                  tile.setSettlement(static_cast<SettlementTypeEnum>(v), kNoSettlement);
              } }
            // [18] resource
            { const int v = features[18];
              if (v >= 0 && v <= 6)
                  tile.setResource(static_cast<ResourcesEnum>(v)); }
            // [19] baseTerrain
            { const int v = features[19];
              if (v >= 0 && v <= 4)
                  tile.setBaseTerrain(static_cast<BaseTerrainEnum>(v)); }
            // [20] tribe
            { const int v = features[20];
              if (v >= 0 && v <= 16)
                  tile.setTribe(static_cast<TribeType>(v)); }

            ++patched;
        }
        invalidateCaches();
        return patched;
    }

private:
    GameEnv(std::shared_ptr<GameSession> session,
            int mapSize,
            std::vector<int> tribes,
            uint32_t seed,
            std::string unitsJsonPath,
            MapType mapType)
        : session_(std::move(session))
        , mapSize_(mapSize)
        , tribesRaw_(std::move(tribes))
        , seed_(seed)
        , unitsJsonPath_(std::move(unitsJsonPath))
        , mapType_(mapType) {}

    void resetCore(bool loadUnits) {
        if (unitsJsonPath_.empty()) unitsJsonPath_ = resolveDefaultUnitsJsonPath();
        if (loadUnits) GameDataSystem::loadUnits(unitsJsonPath_);

        Game game;
        Game::NewGameConfig cfg;
        cfg.mapSize = mapSize_;
        cfg.seed = seed_;
        cfg.mapType = mapType_;
        cfg.tribes = parseTribes(tribesRaw_.empty() ? std::vector<int>{3, 2} : tribesRaw_);
        game.newGame(cfg);

        session_ = std::make_shared<GameSession>(std::move(game));
        session_->replay.clear();
        initializeObservationKnowledgeFromCurrentVisibility();
        session_->events.reset(session_->state.getGame().getPlayers().size());
        invalidateCaches();
    }

    void writeTrainingState(int32_t* out) const {
        ensureState();
        const Game& g = session_->state.getGame();
        const PlayerId pid = session_->state.currentPlayer();
        const Player& player = g.getPlayer(pid);

        int nextTurnStarIncome = 0;
        for (CityId cid : player.getCities()) {
            if (!CitySystem::cityExists(g, cid)) continue;
            if (CitySystem::isCityUnderSiege(g, cid)) continue;
            if (CitySystem::getCityIsInfiltrated(g, cid)) continue;
            nextTurnStarIncome += static_cast<int>(CitySystem::getCityStarsPerRound(g, cid));
            nextTurnStarIncome += StarsSystem::marketIncomeForCity(g, pid, cid);
        }

        const Game::PendingCityUpgrade* pending = g.peekPendingCityUpgrade(pid);
        out[0] = g.getTurnNumber();
        out[1] = static_cast<int32_t>(pid);
        out[2] = g.isGameOver() ? 1 : 0;
        out[3] = g.isGameOver() ? static_cast<int32_t>(g.getWinner()) : -1;
        out[4] = player.getStars();
        out[5] = static_cast<int32_t>(player.getUnits().size());
        out[6] = static_cast<int32_t>(player.getCities().size());
        out[7] = std::max(0, nextTurnStarIncome);
        out[8] = pending ? static_cast<int32_t>(pending->cityId) : -1;
        out[9] = pending ? static_cast<int32_t>(pending->opts.a) : static_cast<int32_t>(CityUpgradeChoice::None);
        out[10] = pending ? static_cast<int32_t>(pending->opts.b) : static_cast<int32_t>(CityUpgradeChoice::None);
    }

    void writePlayerMapTokens(int32_t* out) const {
        ensureState();
        ensureObservationKnowledgeLayout();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const int perspective = static_cast<int>(g.getCurrentPlayerId());
        const std::vector<uint8_t>* knownObs = nullptr;
        if (perspective >= 0) {
            const size_t perspectiveIdx = static_cast<size_t>(perspective);
            if (perspectiveIdx < session_->observations.knownByPlayer.size()) {
                knownObs = &session_->observations.knownByPlayer[perspectiveIdx];
            }
        }

        bool hasClimbing = false;
        bool hasOrganization = false;
        bool hasSailing = false;
        if (perspective >= 0 && perspective < static_cast<int>(g.getPlayers().size())) {
            const Player& p = g.getPlayer(static_cast<PlayerId>(perspective));
            hasClimbing = p.hasTech(TechId::Climbing);
            hasOrganization = p.hasTech(TechId::Organization);
            hasSailing = p.hasTech(TechId::Sailing);
        }

        const auto hasActivatedHide = [](const Unit* unit) {
            if (!unit || !unit->hasSkill(UnitSkill::Hide)) return false;
            const Pos direction = unit->getLastMoveDir();
            return direction.x != 0 || direction.y != 0;
        };
        const auto isEnemyHiddenUnit = [&](const Unit* unit) {
            return unit && perspective >= 0 && perspective < 16 &&
                static_cast<int>(unit->getOwnerId()) != perspective && hasActivatedHide(unit);
        };
        const auto hasAdjacentEnemyHiddenUnit = [&](Pos center) {
            if (perspective < 0 || perspective >= 16) return false;
            static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            for (int i = 0; i < 8; ++i) {
                const Pos nearby{center.x + kDx[i], center.y + kDy[i]};
                if (!m.inBounds(nearby)) continue;
                const UnitId uid = m.unitOn(nearby);
                if (uid != Map::kNoUnit && isEnemyHiddenUnit(g.getUnit(uid))) return true;
            }
            return false;
        };
        const auto canRevealUnitOriginCity = [&](const Unit* unit) {
            if (!unit || unit->getOriginCityId() == kNoCity) return false;
            if (perspective < 0 || perspective >= 16) return false;
            if (static_cast<int>(unit->getOwnerId()) == perspective) return true;
            const City* city = g.getCity(unit->getOriginCityId());
            if (!city || !m.inBounds(city->getPos())) return false;
            const Pos cityPos = city->getPos();
            const uint16_t cityVisibility = static_cast<uint16_t>(m.at(cityPos).getVisibility());
            if ((cityVisibility & (uint16_t(1) << perspective)) == 0) return false;
            const int cityIndex = cityPos.y * w + cityPos.x;
            return !knownObs || (cityIndex >= 0 && static_cast<size_t>(cityIndex) < knownObs->size() &&
                (*knownObs)[static_cast<size_t>(cityIndex)] != 0);
        };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const Tile& tile = m.at(p);
                const int index = y * w + x;
                int32_t* token = out + static_cast<size_t>(index) * kMapTokenFeatureCount;
                const uint16_t visibilityMask = static_cast<uint16_t>(tile.getVisibility());
                const int visibility = (perspective >= 0 && perspective < 16 &&
                    (visibilityMask & (uint16_t(1) << perspective)) != 0) ? 1 : 0;
                bool known = visibility == 1;
                if (known && knownObs && static_cast<size_t>(index) < knownObs->size()) {
                    known = (*knownObs)[static_cast<size_t>(index)] != 0;
                }

                int unitHp = -1;
                int unitOwner = -1;
                int unitType = -1;
                int ownUnitKills = -1;
                int unitMaxHp = -1;
                int unitOriginCity = -1;
                int isCloakAround = 0;
                int capitalLayer = -1;
                int cityLevel = -1;
                int cityOwner = -1;
                int cityUnitsOccupied = -1;
                int cityHasWorkshop = -1;
                int cityHasWall = -1;
                int cityParkCount = -1;
                int resource = static_cast<int>(tile.getResource());
                int settlementType = static_cast<int>(tile.getSettlementType());
                int settlementId = static_cast<int>(tile.getSettlementId()) == static_cast<int>(kNoSettlement)
                    ? -1 : static_cast<int>(tile.getSettlementId());

                if (!hasClimbing && resource == static_cast<int>(ResourcesEnum::Metal)) {
                    resource = static_cast<int>(ResourcesEnum::None);
                }
                if (!hasOrganization && resource == static_cast<int>(ResourcesEnum::Crops)) {
                    resource = static_cast<int>(ResourcesEnum::None);
                }
                if (!hasSailing && settlementType == static_cast<int>(SettlementTypeEnum::Starfish)) {
                    settlementType = static_cast<int>(SettlementTypeEnum::None);
                    settlementId = -1;
                }
                if (tile.getSettlementType() == SettlementTypeEnum::City) {
                    capitalLayer = 0;
                    const CityId cityId = static_cast<CityId>(tile.getSettlementId());
                    if (cityId != kNoCity) {
                        if (const City* city = g.getCity(cityId)) {
                            cityLevel = static_cast<int>(city->getLevel());
                            const PlayerId owner = city->getOwnerId();
                            if (owner != kNoPlayer) cityOwner = static_cast<int>(owner);
                            if (owner != kNoPlayer && static_cast<int>(owner) == perspective) {
                                cityUnitsOccupied = static_cast<int>(CitySystem::getCityUnitsCount(g, cityId));
                            }
                            if (city->isCapitalCity()) capitalLayer = 1;
                            cityHasWorkshop = city->hasWorkshopEnabled() ? 1 : 0;
                            cityHasWall = city->hasCityWallEnabled() ? 1 : 0;
                            cityParkCount = static_cast<int>(city->getParkCount());
                        }
                    }
                }

                const UnitId uid = m.unitOn(p);
                if (uid != Map::kNoUnit) {
                    if (const Unit* unit = g.getUnit(uid)) {
                        if (!isEnemyHiddenUnit(unit)) {
                            unitHp = unit->getHealth();
                            unitOwner = static_cast<int>(unit->getOwnerId());
                            unitType = static_cast<int>(unit->getType());
                            unitMaxHp = unit->getMaxHealth();
                            if (canRevealUnitOriginCity(unit)) unitOriginCity = static_cast<int>(unit->getOriginCityId());
                            if (static_cast<int>(unit->getOwnerId()) == perspective) ownUnitKills = unit->getKillCounter();
                        }
                        if (static_cast<int>(unit->getOwnerId()) == perspective && hasAdjacentEnemyHiddenUnit(p)) {
                            isCloakAround = 1;
                        }
                    }
                }

                token[0] = visibility;
                token[1] = isCloakAround;
                token[2] = unitHp;
                token[3] = unitOwner;
                token[4] = unitType;
                token[5] = ownUnitKills;
                token[6] = static_cast<int>(tile.getTerritoryCityId()) == static_cast<int>(kNoCity)
                    ? -1 : static_cast<int>(tile.getTerritoryCityId());
                token[7] = static_cast<int>(tile.getRoadBridge());
                token[8] = static_cast<int>(tile.getBuildingType());
                token[9] = capitalLayer;
                token[10] = cityHasWorkshop;
                token[11] = cityHasWall;
                token[12] = cityParkCount;
                token[13] = cityLevel;
                token[14] = settlementType;
                token[15] = settlementId;
                token[16] = cityOwner;
                token[17] = cityUnitsOccupied;
                token[18] = resource;
                token[19] = static_cast<int>(tile.getBaseTerrain());
                token[20] = static_cast<int>(tile.getTribe());
                token[21] = unitMaxHp;
                token[22] = unitOriginCity;
                if (!known) {
                    std::fill(token + 1, token + kMapTokenFeatureCount, int32_t{-1});
                }
            }
        }
    }

    void ensureState() const {
        if (!session_) throw std::runtime_error("GameEnv is not initialized.");
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
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const size_t playerCount = g.getPlayers().size();
        const size_t tileCount = static_cast<size_t>(std::max(0, m.getWidth())) * static_cast<size_t>(std::max(0, m.getHeight()));

        if (session_->observations.knownByPlayer.size() != playerCount) {
            const_cast<GameEnv*>(this)->initializeObservationKnowledgeFromCurrentVisibility();
            return;
        }
        for (const auto& row : session_->observations.knownByPlayer) {
            if (row.size() != tileCount) {
                const_cast<GameEnv*>(this)->initializeObservationKnowledgeFromCurrentVisibility();
                return;
            }
        }
    }

    void initializeObservationKnowledgeFromCurrentVisibility() {
        ensureState();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        const int w = m.getWidth();
        const int h = m.getHeight();
        const size_t playerCount = g.getPlayers().size();
        const size_t tileCount = static_cast<size_t>(std::max(0, w)) * static_cast<size_t>(std::max(0, h));

        session_->observations.knownByPlayer.assign(playerCount, std::vector<uint8_t>(tileCount, 0));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const int idx = y * w + x;
                const Tile& t = m.at(p);
                const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());

                for (size_t pid = 0; pid < playerCount && pid < 16; ++pid) {
                    if ((visMask & (uint16_t(1) << pid)) == 0) continue;
                    session_->observations.knownByPlayer[pid][static_cast<size_t>(idx)] = 1;
                }
            }
        }
    }

    void revealObservedTilesForPlayer(PlayerId pid) {
        ensureState();
        const Game& g = session_->state.getGame();
        const Map& m = g.getMap();
        if (pid == kNoPlayer) return;

        const size_t pidIdx = static_cast<size_t>(pid);
        if (pidIdx >= session_->observations.knownByPlayer.size()) return;
        auto& known = session_->observations.knownByPlayer[pidIdx];

        // Ensure the per-player revealed list exists for this player.
        if (session_->observations.lastRevealedByPlayer.size() <= pidIdx)
            session_->observations.lastRevealedByPlayer.resize(pidIdx + 1);
        auto& revealed = session_->observations.lastRevealedByPlayer[pidIdx];

        const int w = m.getWidth();
        const int h = m.getHeight();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Pos p{x, y};
                const int idx = y * w + x;
                const Tile& t = m.at(p);
                const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());
                if ((visMask & (uint16_t(1) << pidIdx)) == 0) continue;
                if (!known[static_cast<size_t>(idx)]) {
                    // 0 → 1 transition: tile newly revealed by this step.
                    revealed.push_back(idx);
                    known[static_cast<size_t>(idx)] = 1;
                }
            }
        }
    }

    void revealObservedTilesForAllPlayers() {
        ensureObservationKnowledgeLayout();
        const Game& g = session_->state.getGame();
        const size_t playerCount = g.getPlayers().size();
        // Reset per-player revealed lists for this step before accumulating.
        session_->observations.lastRevealedByPlayer.assign(playerCount, {});
        for (size_t pid = 0; pid < playerCount; ++pid) {
            revealObservedTilesForPlayer(static_cast<PlayerId>(pid));
        }
    }

    void revealObservedTilesForAllPlayersExcept(PlayerId excluded) {
        ensureObservationKnowledgeLayout();
        const Game& g = session_->state.getGame();
        const size_t playerCount = g.getPlayers().size();
        // Reset per-player revealed lists. The excluded player's list stays empty.
        session_->observations.lastRevealedByPlayer.assign(playerCount, {});
        for (size_t pid = 0; pid < playerCount; ++pid) {
            if (static_cast<PlayerId>(pid) == excluded) continue;
            revealObservedTilesForPlayer(static_cast<PlayerId>(pid));
        }
    }

    void ensureLegalIdsCache() const {
        const PlayerId pid = session_->state.currentPlayer();
        if (legalIdsCacheValid_ && legalIdsCachePid_ == pid) return;
        legalIdsCache_ = session_->state.legalActionIds(pid);
        legalIdsCachePid_ = pid;
        legalIdsCacheValid_ = true;
        legalMaskCacheValid_ = false;
    }

    void ensureLegalMaskCache() const {
        const PlayerId pid = session_->state.currentPlayer();
        if (legalMaskCacheValid_ && legalMaskCachePid_ == pid) return;

        ensureLegalIdsCache();

        legalMaskCache_.assign(session_->state.actionSpace().size(), 0);
        for (size_t id : legalIdsCache_) {
            if (id < legalMaskCache_.size()) legalMaskCache_[id] = 1;
        }
        legalMaskCachePid_ = pid;
        legalMaskCacheValid_ = true;
    }

    std::shared_ptr<GameSession> session_;
    int mapSize_ = 16;
    std::vector<int> tribesRaw_{3, 2};
    uint32_t seed_ = 1;
    std::string unitsJsonPath_;
    MapType mapType_ = MapType::Lakes;
    mutable bool legalIdsCacheValid_ = false;
    mutable PlayerId legalIdsCachePid_ = kNoPlayer;
    mutable std::vector<size_t> legalIdsCache_;
    mutable bool legalMaskCacheValid_ = false;
    mutable PlayerId legalMaskCachePid_ = kNoPlayer;
    mutable std::vector<uint8_t> legalMaskCache_;
};

// A small fixed worker pool dedicated to one VectorGameEnv. It deliberately
// runs one batch job at a time; this avoids nested parallelism with PyTorch and
// keeps GameSession ownership strictly one-env-per-worker-task.
class BatchThreadPool {
public:
    explicit BatchThreadPool(size_t workerCount) {
        if (workerCount <= 1) return;
        workers_.reserve(workerCount);
        for (size_t i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    BatchThreadPool(const BatchThreadPool&) = delete;
    BatchThreadPool& operator=(const BatchThreadPool&) = delete;

    ~BatchThreadPool() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        workReady_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    size_t workerCount() const { return workers_.empty() ? 1 : workers_.size(); }

    template <typename Fn>
    void parallelFor(size_t count, Fn&& fn) {
        if (count == 0) return;
        if (workers_.empty() || count == 1) {
            fn(0, count);
            return;
        }

        {
            std::lock_guard lock(mutex_);
            task_ = std::forward<Fn>(fn);
            taskCount_ = count;
            nextIndex_.store(0, std::memory_order_relaxed);
            completedWorkers_ = 0;
            workerException_ = nullptr;
            ++generation_;
        }
        workReady_.notify_all();

        std::unique_lock lock(mutex_);
        workDone_.wait(lock, [this] { return completedWorkers_ == workers_.size(); });
        if (workerException_) std::rethrow_exception(workerException_);
    }

private:
    void workerLoop() {
        size_t seenGeneration = 0;
        for (;;) {
            {
                std::unique_lock lock(mutex_);
                workReady_.wait(lock, [this, &seenGeneration] {
                    return stopping_ || generation_ != seenGeneration;
                });
                if (stopping_) return;
                seenGeneration = generation_;
            }

            try {
                for (;;) {
                    const size_t begin = nextIndex_.fetch_add(1, std::memory_order_relaxed);
                    if (begin >= taskCount_) break;
                    task_(begin, begin + 1);
                }
            } catch (...) {
                std::lock_guard lock(mutex_);
                if (!workerException_) workerException_ = std::current_exception();
            }

            {
                std::lock_guard lock(mutex_);
                ++completedWorkers_;
                if (completedWorkers_ == workers_.size()) workDone_.notify_one();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable workReady_;
    std::condition_variable workDone_;
    std::function<void(size_t, size_t)> task_;
    std::atomic<size_t> nextIndex_{0};
    size_t taskCount_ = 0;
    size_t completedWorkers_ = 0;
    size_t generation_ = 0;
    bool stopping_ = false;
    std::exception_ptr workerException_;
};

class VectorGameEnv {
public:
    VectorGameEnv(
        int numEnvs,
        int mapSize,
        const std::vector<int>& tribes,
        uint32_t seed,
        const std::string& unitsJsonPath,
        MapType mapType,
        int numThreads,
        int maxActions,
        bool autoReset,
        bool includeCombatPreview,
        int visibleEventHistory)
        : numEnvs_(numEnvs)
        , mapSize_(mapSize)
        , tribes_(tribes.empty() ? std::vector<int>{3, 2} : tribes)
        , baseSeed_(seed)
        , unitsJsonPath_(unitsJsonPath.empty() ? resolveDefaultUnitsJsonPath() : unitsJsonPath)
        , mapType_(mapType)
        , maxActions_(maxActions)
        , autoReset_(autoReset)
        , includeCombatPreview_(includeCombatPreview)
        , visibleEventHistory_(visibleEventHistory)
        , pool_(resolveThreadCount(numEnvs, numThreads))
    {
        if (numEnvs_ <= 0) throw std::invalid_argument("num_envs must be positive");
        if (mapSize_ <= 0) throw std::invalid_argument("map_size must be positive");
        if (maxActions_ <= 0) throw std::invalid_argument("max_actions must be positive");
        if (visibleEventHistory_ < 0) throw std::invalid_argument("visible_event_history must be non-negative");

        // Unit templates are process-global and immutable during vectorized
        // execution. Load them before any environment worker can begin.
        GameDataSystem::loadUnits(unitsJsonPath_);
        envs_.reserve(static_cast<size_t>(numEnvs_));
        resetCounts_.assign(static_cast<size_t>(numEnvs_), 0);
        for (int i = 0; i < numEnvs_; ++i) {
            envs_.emplace_back(mapSize_, tribes_, seedFor(static_cast<size_t>(i)), unitsJsonPath_, mapType_);
        }
        if (envs_.front().actionSpaceSize() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            throw std::invalid_argument("map_size produces action ids too large for VectorGameEnv int32 batches");
        }
    }

    VectorGameEnv(const VectorGameEnv&) = delete;
    VectorGameEnv& operator=(const VectorGameEnv&) = delete;

    py::dict reset(std::optional<uint32_t> seed = std::nullopt) {
        BatchArrays out(static_cast<size_t>(numEnvs_), tileCount(), static_cast<size_t>(maxActions_),
                        static_cast<size_t>(visibleEventHistory_));
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            if (seed) {
                baseSeed_ = *seed;
                std::fill(resetCounts_.begin(), resetCounts_.end(), 0);
            }
            for (size_t i = 0; i < envs_.size(); ++i) resetOne(i);
            fillBatch(out, nullptr, false);
        }
        return out.intoDict();
    }

    py::dict step(py::array actionIds) {
        auto actions = py::array_t<int32_t, py::array::c_style | py::array::forcecast>::ensure(actionIds);
        if (!actions || actions.ndim() != 1 || actions.shape(0) != numEnvs_) {
            throw std::invalid_argument("action_ids must be a contiguous one-dimensional int32-compatible array with num_envs entries");
        }

        BatchArrays out(static_cast<size_t>(numEnvs_), tileCount(), static_cast<size_t>(maxActions_),
                        static_cast<size_t>(visibleEventHistory_));
        const int32_t* actionData = actions.data();
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            fillBatch(out, actionData, true);
        }
        return out.intoDict();
    }

    // Fill caller-owned arrays in place. This is intentionally CPU/framework
    // agnostic: callers may use ordinary NumPy storage or NumPy views backed
    // by pinned tensors from their own PyTorch/JAX runtime.
    void resetInto(const py::dict& buffers, std::optional<uint32_t> seed = std::nullopt) {
        BatchArrays out(buffers, static_cast<size_t>(numEnvs_), tileCount(),
                        static_cast<size_t>(maxActions_), static_cast<size_t>(visibleEventHistory_));
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            if (seed) {
                baseSeed_ = *seed;
                std::fill(resetCounts_.begin(), resetCounts_.end(), 0);
            }
            for (size_t i = 0; i < envs_.size(); ++i) resetOne(i);
            fillBatch(out, nullptr, false);
        }
    }

    void stepInto(py::array actionIds, const py::dict& buffers) {
        // Unlike step(), this boundary never force-casts: a conversion here
        // could silently create a pageable temporary and defeat the caller's
        // pinned-memory transfer plan. Strided inputs are fine because the
        // tiny action vector is copied before C++ releases the GIL.
        if (!actionIds.dtype().is(py::dtype::of<int32_t>()) ||
            actionIds.ndim() != 1 || actionIds.shape(0) != numEnvs_) {
            throw std::invalid_argument(
                "action_ids for step_into must be a one-dimensional int32 array with num_envs entries");
        }
        const auto actions = py::reinterpret_borrow<py::array_t<int32_t>>(actionIds);
        // Validate every output before advancing any game. Copy the small
        // action vector so callers may safely select actions from the same
        // output buffer that will be overwritten by this call.
        BatchArrays out(buffers, static_cast<size_t>(numEnvs_), tileCount(),
                        static_cast<size_t>(maxActions_), static_cast<size_t>(visibleEventHistory_));
        std::vector<int32_t> copiedActions(static_cast<size_t>(numEnvs_));
        const auto actionView = actions.unchecked<1>();
        for (py::ssize_t i = 0; i < actionView.shape(0); ++i) {
            copiedActions[static_cast<size_t>(i)] = actionView(i);
        }
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            fillBatch(out, copiedActions.data(), true);
        }
    }

    py::dict batchSpec() const {
        const py::ssize_t batch = static_cast<py::ssize_t>(numEnvs_);
        const py::ssize_t tiles = static_cast<py::ssize_t>(tileCount());
        const py::ssize_t actions = static_cast<py::ssize_t>(maxActions_);
        const py::ssize_t history = static_cast<py::ssize_t>(visibleEventHistory_);
        py::dict out;
        const auto add = [&](const char* name, const py::dtype& dtype, py::tuple shape) {
            py::dict field;
            field["dtype"] = dtype;
            field["shape"] = std::move(shape);
            out[name] = std::move(field);
        };
        add("map_tokens", py::dtype::of<int32_t>(), py::make_tuple(batch, tiles, kMapTokenFeatureCount));
        add("state", py::dtype::of<int32_t>(), py::make_tuple(batch, kVectorStateFeatureCount));
        add("action_id", py::dtype::of<int32_t>(), py::make_tuple(batch, actions));
        add("action_features", py::dtype::of<int32_t>(), py::make_tuple(batch, actions, kVectorActionFeatureCount));
        add("action_arg_mask", py::dtype::of<uint8_t>(), py::make_tuple(batch, actions, kVectorActionArgMaskCount));
        add("action_mask", py::dtype::of<uint8_t>(), py::make_tuple(batch, actions));
        add("visible_event_features", py::dtype::of<int32_t>(), py::make_tuple(batch, history, kVisibleEventFeatureCount));
        add("visible_event_sequence", py::dtype::of<uint64_t>(), py::make_tuple(batch, history));
        add("visible_event_action_sequence", py::dtype::of<uint64_t>(), py::make_tuple(batch, history));
        add("visible_event_mask", py::dtype::of<uint8_t>(), py::make_tuple(batch, history));
        add("visible_event_affected", py::dtype::of<int32_t>(),
            py::make_tuple(batch, history, kVisibleEventAffectedUnitCount, kVisibleEventAffectedFeatureCount));
        add("visible_event_affected_mask", py::dtype::of<uint8_t>(),
            py::make_tuple(batch, history, kVisibleEventAffectedUnitCount));
        add("legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("total_legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("action_truncated", py::dtype::of<uint8_t>(), py::make_tuple(batch));
        add("reward", py::dtype::of<float>(), py::make_tuple(batch));
        add("terminated", py::dtype::of<uint8_t>(), py::make_tuple(batch));
        add("action_valid", py::dtype::of<uint8_t>(), py::make_tuple(batch));
        add("env_id", py::dtype::of<int32_t>(), py::make_tuple(batch));
        return out;
    }

    int numEnvs() const { return numEnvs_; }
    int numThreads() const { return static_cast<int>(pool_.workerCount()); }
    int mapSize() const { return mapSize_; }
    int maxActions() const { return maxActions_; }
    bool autoReset() const { return autoReset_; }
    bool includeCombatPreview() const { return includeCombatPreview_; }
    int visibleEventHistory() const { return visibleEventHistory_; }

    std::vector<std::string> actionFeatureNames() const {
        return {
            "type_id", "source_index", "target_index", "city", "tech", "building",
            "spawn_type", "upgrade", "tile_action", "unit_upgrade", "unit_id",
            "population_gain", "cost_stars", "stars_before", "affordable",
            "damage_dealt", "damage_received",
        };
    }

    std::vector<std::string> stateFeatureNames() const {
        return {
            "turn", "current_player", "game_over", "winner", "player_stars",
            "owns_units", "own_cities", "next_turn_star_income", "pending_city_id",
            "pending_upgrade_a", "pending_upgrade_b",
        };
    }

    std::vector<std::string> visibleEventFeatureNames() const {
        return {
            "round", "turn", "type_id", "flags", "source_index", "target_index",
            "damage", "hp_before", "hp_after", "actor_player", "actor_tribe",
            "tile_action_kind", "building_type", "spawn_type", "source_unit_type",
            "target_unit_type", "source_observed_unit_id", "target_observed_unit_id",
            "source_unit_hp_before", "source_unit_hp_after", "target_unit_hp_before",
            "target_unit_hp_after", "unit_upgrade_kind", "upgraded_unit_type",
            "unit_destroyed", "source_unit_destroyed",
        };
    }

    std::vector<std::string> visibleEventAffectedFeatureNames() const {
        return {
            "observed_unit_id", "tile_index", "unit_type", "damage", "hp_before",
            "hp_after", "destroyed", "splash",
        };
    }

private:
    struct BatchArrays {
        BatchArrays(size_t numEnvs, size_t tiles, size_t maxActions, size_t visibleEventHistory)
            : mapTokens({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(tiles),
                         static_cast<py::ssize_t>(kMapTokenFeatureCount)})
            , state({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(kVectorStateFeatureCount)})
            , actionIds({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions)})
            , actionFeatures({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions),
                              static_cast<py::ssize_t>(kVectorActionFeatureCount)})
            , actionArgMasks({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions),
                              static_cast<py::ssize_t>(kVectorActionArgMaskCount)})
            , actionMask({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions)})
            , visibleEventFeatures({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(visibleEventHistory),
                                    static_cast<py::ssize_t>(kVisibleEventFeatureCount)})
            , visibleEventSequence({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(visibleEventHistory)})
            , visibleEventActionSequence({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(visibleEventHistory)})
            , visibleEventMask({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(visibleEventHistory)})
            , visibleEventAffected({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(visibleEventHistory),
                                    static_cast<py::ssize_t>(kVisibleEventAffectedUnitCount),
                                    static_cast<py::ssize_t>(kVisibleEventAffectedFeatureCount)})
            , visibleEventAffectedMask({static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(visibleEventHistory),
                                        static_cast<py::ssize_t>(kVisibleEventAffectedUnitCount)})
            , legalActionCount(static_cast<py::ssize_t>(numEnvs))
            , totalLegalActionCount(static_cast<py::ssize_t>(numEnvs))
            , actionTruncated(static_cast<py::ssize_t>(numEnvs))
            , reward(static_cast<py::ssize_t>(numEnvs))
            , terminated(static_cast<py::ssize_t>(numEnvs))
            , actionValid(static_cast<py::ssize_t>(numEnvs))
            , envId(static_cast<py::ssize_t>(numEnvs)) {}

        // This constructor intentionally only borrows existing arrays. It
        // performs no dtype coercion, contiguity repair, or allocation, so a
        // pinned external buffer remains the exact storage written by C++.
        BatchArrays(const py::dict& buffers, size_t numEnvs, size_t tiles,
                    size_t maxActions, size_t visibleEventHistory)
            : mapTokens(requireWritableBatchArray<int32_t>(
                buffers, "map_tokens", {static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(tiles),
                                         static_cast<py::ssize_t>(kMapTokenFeatureCount)}))
            , state(requireWritableBatchArray<int32_t>(
                buffers, "state", {static_cast<py::ssize_t>(numEnvs),
                                    static_cast<py::ssize_t>(kVectorStateFeatureCount)}))
            , actionIds(requireWritableBatchArray<int32_t>(
                buffers, "action_id", {static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions)}))
            , actionFeatures(requireWritableBatchArray<int32_t>(
                buffers, "action_features", {static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions),
                                              static_cast<py::ssize_t>(kVectorActionFeatureCount)}))
            , actionArgMasks(requireWritableBatchArray<uint8_t>(
                buffers, "action_arg_mask", {static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions),
                                              static_cast<py::ssize_t>(kVectorActionArgMaskCount)}))
            , actionMask(requireWritableBatchArray<uint8_t>(
                buffers, "action_mask", {static_cast<py::ssize_t>(numEnvs), static_cast<py::ssize_t>(maxActions)}))
            , visibleEventFeatures(requireWritableBatchArray<int32_t>(
                buffers, "visible_event_features", {static_cast<py::ssize_t>(numEnvs),
                                                     static_cast<py::ssize_t>(visibleEventHistory),
                                                     static_cast<py::ssize_t>(kVisibleEventFeatureCount)}))
            , visibleEventSequence(requireWritableBatchArray<uint64_t>(
                buffers, "visible_event_sequence", {static_cast<py::ssize_t>(numEnvs),
                                                     static_cast<py::ssize_t>(visibleEventHistory)}))
            , visibleEventActionSequence(requireWritableBatchArray<uint64_t>(
                buffers, "visible_event_action_sequence", {static_cast<py::ssize_t>(numEnvs),
                                                            static_cast<py::ssize_t>(visibleEventHistory)}))
            , visibleEventMask(requireWritableBatchArray<uint8_t>(
                buffers, "visible_event_mask", {static_cast<py::ssize_t>(numEnvs),
                                                 static_cast<py::ssize_t>(visibleEventHistory)}))
            , visibleEventAffected(requireWritableBatchArray<int32_t>(
                buffers, "visible_event_affected", {static_cast<py::ssize_t>(numEnvs),
                                                     static_cast<py::ssize_t>(visibleEventHistory),
                                                     static_cast<py::ssize_t>(kVisibleEventAffectedUnitCount),
                                                     static_cast<py::ssize_t>(kVisibleEventAffectedFeatureCount)}))
            , visibleEventAffectedMask(requireWritableBatchArray<uint8_t>(
                buffers, "visible_event_affected_mask", {static_cast<py::ssize_t>(numEnvs),
                                                          static_cast<py::ssize_t>(visibleEventHistory),
                                                          static_cast<py::ssize_t>(kVisibleEventAffectedUnitCount)}))
            , legalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "legal_action_count", {static_cast<py::ssize_t>(numEnvs)}))
            , totalLegalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "total_legal_action_count", {static_cast<py::ssize_t>(numEnvs)}))
            , actionTruncated(requireWritableBatchArray<uint8_t>(
                buffers, "action_truncated", {static_cast<py::ssize_t>(numEnvs)}))
            , reward(requireWritableBatchArray<float>(
                buffers, "reward", {static_cast<py::ssize_t>(numEnvs)}))
            , terminated(requireWritableBatchArray<uint8_t>(
                buffers, "terminated", {static_cast<py::ssize_t>(numEnvs)}))
            , actionValid(requireWritableBatchArray<uint8_t>(
                buffers, "action_valid", {static_cast<py::ssize_t>(numEnvs)}))
            , envId(requireWritableBatchArray<int32_t>(
                buffers, "env_id", {static_cast<py::ssize_t>(numEnvs)}))
        {
            validateNonOverlappingStorage();
        }

        py::dict intoDict() {
            py::dict out;
            out["map_tokens"] = std::move(mapTokens);
            out["state"] = std::move(state);
            out["action_id"] = std::move(actionIds);
            out["action_features"] = std::move(actionFeatures);
            out["action_arg_mask"] = std::move(actionArgMasks);
            out["action_mask"] = std::move(actionMask);
            out["visible_event_features"] = std::move(visibleEventFeatures);
            out["visible_event_sequence"] = std::move(visibleEventSequence);
            out["visible_event_action_sequence"] = std::move(visibleEventActionSequence);
            out["visible_event_mask"] = std::move(visibleEventMask);
            out["visible_event_affected"] = std::move(visibleEventAffected);
            out["visible_event_affected_mask"] = std::move(visibleEventAffectedMask);
            out["legal_action_count"] = std::move(legalActionCount);
            out["total_legal_action_count"] = std::move(totalLegalActionCount);
            out["action_truncated"] = std::move(actionTruncated);
            out["reward"] = std::move(reward);
            out["terminated"] = std::move(terminated);
            out["action_valid"] = std::move(actionValid);
            out["env_id"] = std::move(envId);
            return out;
        }

        py::array_t<int32_t> mapTokens;
        py::array_t<int32_t> state;
        py::array_t<int32_t> actionIds;
        py::array_t<int32_t> actionFeatures;
        py::array_t<uint8_t> actionArgMasks;
        py::array_t<uint8_t> actionMask;
        py::array_t<int32_t> visibleEventFeatures;
        py::array_t<uint64_t> visibleEventSequence;
        py::array_t<uint64_t> visibleEventActionSequence;
        py::array_t<uint8_t> visibleEventMask;
        py::array_t<int32_t> visibleEventAffected;
        py::array_t<uint8_t> visibleEventAffectedMask;
        py::array_t<int32_t> legalActionCount;
        py::array_t<int32_t> totalLegalActionCount;
        py::array_t<uint8_t> actionTruncated;
        py::array_t<float> reward;
        py::array_t<uint8_t> terminated;
        py::array_t<uint8_t> actionValid;
        py::array_t<int32_t> envId;

    private:
        void validateNonOverlappingStorage() const {
            // Keep this fixed-size metadata on the stack. Buffer validation
            // happens once per API call, before the hot native step begins.
            const std::array<std::pair<const char*, const py::array*>, 19> arrays{{
                {"map_tokens", &mapTokens},
                {"state", &state},
                {"action_id", &actionIds},
                {"action_features", &actionFeatures},
                {"action_arg_mask", &actionArgMasks},
                {"action_mask", &actionMask},
                {"visible_event_features", &visibleEventFeatures},
                {"visible_event_sequence", &visibleEventSequence},
                {"visible_event_action_sequence", &visibleEventActionSequence},
                {"visible_event_mask", &visibleEventMask},
                {"visible_event_affected", &visibleEventAffected},
                {"visible_event_affected_mask", &visibleEventAffectedMask},
                {"legal_action_count", &legalActionCount},
                {"total_legal_action_count", &totalLegalActionCount},
                {"action_truncated", &actionTruncated},
                {"reward", &reward},
                {"terminated", &terminated},
                {"action_valid", &actionValid},
                {"env_id", &envId},
            }};
            validateNonOverlappingBatchStorage(arrays);
        }
    };

    static size_t resolveThreadCount(int numEnvs, int requested) {
        const unsigned int hardware = std::max(1u, std::thread::hardware_concurrency());
        const size_t wanted = requested > 0 ? static_cast<size_t>(requested) : static_cast<size_t>(hardware);
        return std::max<size_t>(1, std::min(static_cast<size_t>(std::max(1, numEnvs)), wanted));
    }

    size_t tileCount() const {
        return static_cast<size_t>(mapSize_) * static_cast<size_t>(mapSize_);
    }

    uint32_t seedFor(size_t envIndex) const {
        if (baseSeed_ == 0) return 0;
        return baseSeed_ + static_cast<uint32_t>(envIndex) +
            static_cast<uint32_t>(resetCounts_[envIndex] * static_cast<uint64_t>(numEnvs_));
    }

    void resetOne(size_t envIndex) {
        envs_[envIndex].resetNative(seedFor(envIndex));
        ++resetCounts_[envIndex];
    }

    void fillBatch(BatchArrays& out, const int32_t* actions, bool advance) {
        const size_t numEnvs = envs_.size();
        const size_t actionSlots = numEnvs * static_cast<size_t>(maxActions_);
        const size_t historyLength = static_cast<size_t>(visibleEventHistory_);
        const size_t historyRows = numEnvs * historyLength;
        std::fill_n(out.actionIds.mutable_data(), actionSlots, int32_t{-1});
        std::fill_n(out.actionFeatures.mutable_data(), actionSlots * kVectorActionFeatureCount, int32_t{0});
        std::fill_n(out.actionArgMasks.mutable_data(), actionSlots * kVectorActionArgMaskCount, uint8_t{0});
        std::fill_n(out.actionMask.mutable_data(), actionSlots, uint8_t{0});
        std::fill_n(out.visibleEventFeatures.mutable_data(), historyRows * kVisibleEventFeatureCount, int32_t{0});
        std::fill_n(out.visibleEventSequence.mutable_data(), historyRows, uint64_t{0});
        std::fill_n(out.visibleEventActionSequence.mutable_data(), historyRows, uint64_t{0});
        std::fill_n(out.visibleEventMask.mutable_data(), historyRows, uint8_t{0});
        std::fill_n(out.visibleEventAffected.mutable_data(),
                    historyRows * kVisibleEventAffectedUnitCount * kVisibleEventAffectedFeatureCount, int32_t{0});
        std::fill_n(out.visibleEventAffectedMask.mutable_data(),
                    historyRows * kVisibleEventAffectedUnitCount, uint8_t{0});
        std::fill_n(out.reward.mutable_data(), numEnvs, 0.0f);
        std::fill_n(out.terminated.mutable_data(), numEnvs, uint8_t{0});
        std::fill_n(out.actionValid.mutable_data(), numEnvs, uint8_t{0});

        int32_t* mapData = out.mapTokens.mutable_data();
        int32_t* stateData = out.state.mutable_data();
        int32_t* idsData = out.actionIds.mutable_data();
        int32_t* featuresData = out.actionFeatures.mutable_data();
        uint8_t* argMasksData = out.actionArgMasks.mutable_data();
        uint8_t* maskData = out.actionMask.mutable_data();
        int32_t* visibleEventFeaturesData = out.visibleEventFeatures.mutable_data();
        uint64_t* visibleEventSequenceData = out.visibleEventSequence.mutable_data();
        uint64_t* visibleEventActionSequenceData = out.visibleEventActionSequence.mutable_data();
        uint8_t* visibleEventMaskData = out.visibleEventMask.mutable_data();
        int32_t* visibleEventAffectedData = out.visibleEventAffected.mutable_data();
        uint8_t* visibleEventAffectedMaskData = out.visibleEventAffectedMask.mutable_data();
        int32_t* countData = out.legalActionCount.mutable_data();
        int32_t* totalCountData = out.totalLegalActionCount.mutable_data();
        uint8_t* truncatedData = out.actionTruncated.mutable_data();
        float* rewardData = out.reward.mutable_data();
        uint8_t* terminatedData = out.terminated.mutable_data();
        uint8_t* validData = out.actionValid.mutable_data();
        int32_t* envIdData = out.envId.mutable_data();
        const size_t tiles = tileCount();

        pool_.parallelFor(numEnvs, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (advance) {
                    const int32_t actionId = actions[i];
                    const NativeStepResult result = actionId >= 0
                        ? envs_[i].stepNative(static_cast<size_t>(actionId))
                        : NativeStepResult{};
                    validData[i] = result.ok ? 1 : 0;
                    rewardData[i] = result.reward;
                    terminatedData[i] = result.done ? 1 : 0;
                    if (result.ok && result.done && autoReset_) resetOne(i);
                }

                envs_[i].writeTrainingPacket(
                    mapData + i * tiles * kMapTokenFeatureCount,
                    stateData + i * kVectorStateFeatureCount,
                    idsData + i * static_cast<size_t>(maxActions_),
                    featuresData + i * static_cast<size_t>(maxActions_) * kVectorActionFeatureCount,
                    argMasksData + i * static_cast<size_t>(maxActions_) * kVectorActionArgMaskCount,
                    maskData + i * static_cast<size_t>(maxActions_),
                    countData + i,
                    totalCountData + i,
                    truncatedData + i,
                    static_cast<size_t>(maxActions_),
                    includeCombatPreview_);
                if (historyLength > 0) {
                    envs_[i].writeVisibleEventHistory(
                        visibleEventFeaturesData + i * historyLength * kVisibleEventFeatureCount,
                        visibleEventSequenceData + i * historyLength,
                        visibleEventActionSequenceData + i * historyLength,
                        visibleEventMaskData + i * historyLength,
                        visibleEventAffectedData +
                            i * historyLength * kVisibleEventAffectedUnitCount * kVisibleEventAffectedFeatureCount,
                        visibleEventAffectedMaskData + i * historyLength * kVisibleEventAffectedUnitCount,
                        historyLength);
                }
                // A vector environment exposes a bounded model input rather
                // than GameEnv's unbounded cursor API. Trim every observer's
                // stream after encoding so long training jobs keep O(B * K)
                // event memory.
                envs_[i].retainVisibleEventHistory(historyLength);
                envIdData[i] = static_cast<int32_t>(i);
            }
        });

    }

    int numEnvs_ = 0;
    int mapSize_ = 0;
    std::vector<int> tribes_;
    uint32_t baseSeed_ = 0;
    std::string unitsJsonPath_;
    MapType mapType_ = MapType::Lakes;
    int maxActions_ = 0;
    bool autoReset_ = true;
    bool includeCombatPreview_ = false;
    int visibleEventHistory_ = 0;
    std::vector<GameEnv> envs_;
    std::vector<uint64_t> resetCounts_;
    BatchThreadPool pool_;
    std::mutex apiMutex_;
};

// Native multi-root PUCT scheduler. Python sees only whole leaf batches and
// submits one policy/value result per leaf; selection, state branching and
// backup never cross the Python boundary.
class MctsPool {
public:
    friend class SelfPlayPool;

    MctsPool(py::iterable roots, int numThreads, int maxActions, float cPuct)
        : MctsPool(copyRootsFromPython(roots), numThreads, maxActions, cPuct) {}

    // Native owners (SelfPlayPool) already hold detached belief worlds. Taking
    // those roots by value avoids a Python round trip and an unnecessary
    // second clone before the first MCTS expansion.
    MctsPool(std::vector<GameEnv> roots, int numThreads, int maxActions, float cPuct)
        : maxActions_(maxActions)
        , cPuct_(cPuct)
    {
        validateSettings(numThreads);
        initializeRoots(std::move(roots));
        ownedPool_ = std::make_unique<BatchThreadPool>(resolveThreadCount(trees_.size(), numThreads));
        pool_ = ownedPool_.get();
    }

    // A SelfPlayPool never runs live simulation and MCTS work concurrently,
    // so sharing one fixed worker pool avoids CPU oversubscription and worker
    // creation/destruction after every real move.
    MctsPool(std::vector<GameEnv> roots,
             BatchThreadPool& sharedPool,
             int maxActions,
             float cPuct)
        : maxActions_(maxActions)
        , cPuct_(cPuct)
        , pool_(&sharedPool)
    {
        validateSettings(0);
        initializeRoots(std::move(roots));
    }

    MctsPool(const MctsPool&) = delete;
    MctsPool& operator=(const MctsPool&) = delete;

    // Reuse the scheduler and its worker threads for a new generation of
    // detached roots. Pending leaves are deliberately rejected: accepting a
    // late model result for another position would corrupt a search tree.
    void resetRoots(std::vector<GameEnv> roots) {
        std::lock_guard lock(apiMutex_);
        if (!pending_.empty()) {
            throw std::logic_error("cannot replace MCTS roots while leaf evaluations are pending");
        }
        initializeRoots(std::move(roots));
        nextTree_ = 0;
    }

    // Lifecycle owners use this only when a real position is reset or
    // advanced. It deliberately invalidates late evaluator outputs; leaf ids
    // remain monotonic, so an old id can never name a new node.
    void replaceRoots(std::vector<GameEnv> roots) {
        std::lock_guard lock(apiMutex_);
        initializeRoots(std::move(roots));
        nextTree_ = 0;
    }

    py::dict selectLeaves(int maxLeaves = 0) {
        const size_t limit = leafCapacity(maxLeaves);
        std::unique_lock lock(apiMutex_, std::defer_lock);
        std::vector<Selection> selections;
        try {
            {
                py::gil_scoped_release release;
                lock.lock();
                selections = selectPendingLeaves(limit);
            }

            LeafBatch out(selections.size(), tileCount(), static_cast<size_t>(maxActions_));
            {
                py::gil_scoped_release release;
                writeLeafBatch(out, selections);
            }
            py::dict result = out.intoDict();
            lock.unlock();
            return result;
        } catch (...) {
            if (lock.owns_lock()) cancelSelections(selections);
            throw;
        }
    }

    // Caller-owned leaf batch. Its capacity is fixed before selection so it
    // can be pinned and reused by a framework-owned double buffer. The number
    // of selectable leaves is dynamic; only rows [0:returned_leaf_count] are
    // valid on return.
    int selectLeavesInto(const py::dict& buffers, int maxLeaves = 0) {
        const size_t capacity = leafCapacity(maxLeaves);
        LeafBatch out(buffers, capacity, tileCount(), static_cast<size_t>(maxActions_));
        std::unique_lock lock(apiMutex_, std::defer_lock);
        std::vector<Selection> selections;
        try {
            {
                py::gil_scoped_release release;
                lock.lock();
                selections = selectPendingLeaves(capacity);
            }
            {
                py::gil_scoped_release release;
                writeLeafBatch(out, selections);
            }
            lock.unlock();
            return static_cast<int>(selections.size());
        } catch (...) {
            if (lock.owns_lock()) cancelSelections(selections);
            throw;
        }
    }

    void expandAndBackup(py::array leafIds, py::array policyLogits, py::array values) {
        auto ids = py::array_t<uint64_t, py::array::c_style | py::array::forcecast>::ensure(leafIds);
        auto logits = py::array_t<float, py::array::c_style | py::array::forcecast>::ensure(policyLogits);
        auto valueArray = py::array_t<float, py::array::c_style | py::array::forcecast>::ensure(values);
        if (!ids || ids.ndim() != 1) {
            throw std::invalid_argument("leaf_ids must be a contiguous one-dimensional uint64-compatible array");
        }
        if (!logits || logits.ndim() != 2 || logits.shape(0) != ids.shape(0) || logits.shape(1) != maxActions_) {
            throw std::invalid_argument("policy_logits must have shape [leaf_count, max_actions]");
        }
        const bool twoPlayer = twoPlayerZeroSum_;
        const size_t valuesPerLeaf = twoPlayer ? 1 : static_cast<size_t>(playerCount_);
        if (!valueArray ||
            (twoPlayer && (valueArray.ndim() != 1 || valueArray.shape(0) != ids.shape(0))) ||
            (!twoPlayer && (valueArray.ndim() != 2 || valueArray.shape(0) != ids.shape(0) ||
                            valueArray.shape(1) != playerCount_))) {
            throw std::invalid_argument(
                twoPlayer
                    ? "values must have shape [leaf_count] for a two-player MctsPool"
                    : "values must have shape [leaf_count, player_count] for a multi-player MctsPool");
        }

        const size_t count = static_cast<size_t>(ids.shape(0));
        const uint64_t* idData = ids.data();
        const float* logitsData = logits.data();
        const float* valueData = valueArray.data();
        for (size_t row = 0; row < count; ++row) {
            for (size_t player = 0; player < valuesPerLeaf; ++player) {
                const float value = valueData[row * valuesPerLeaf + player];
                if (!std::isfinite(value) || value < -1.0f || value > 1.0f) {
                    throw std::invalid_argument("every MCTS value must be finite and in [-1, 1]");
                }
            }
            for (int col = 0; col < maxActions_; ++col) {
                const float logit = logitsData[row * static_cast<size_t>(maxActions_) + static_cast<size_t>(col)];
                if (std::isnan(logit) || logit == std::numeric_limits<float>::infinity()) {
                    throw std::invalid_argument("policy_logits may contain finite values or -inf, but not NaN/+inf");
                }
            }
        }

        std::unique_lock lock(apiMutex_, std::defer_lock);
        std::vector<PendingLeaf> pending;
        {
            py::gil_scoped_release release;
            lock.lock();
            pending = resolvePendingLeaves(idData, count);
            try {
                pool_->parallelFor(pending.size(), [&](size_t begin, size_t end) {
                    for (size_t row = begin; row < end; ++row) {
                        expandOne(pending[row],
                                  logitsData + row * static_cast<size_t>(maxActions_),
                                  valueData + row * valuesPerLeaf);
                    }
                });
            } catch (...) {
                clearPending(pending);
                throw;
            }
            clearPending(pending);
        }
    }

    py::dict rootPolicy(float temperature = 1.0f) {
        if (!std::isfinite(temperature) || temperature < 0.0f) {
            throw std::invalid_argument("temperature must be finite and non-negative");
        }

        RootBatch out(trees_.size(), static_cast<size_t>(maxActions_));
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            pool_->parallelFor(trees_.size(), [&](size_t begin, size_t end) {
                for (size_t tree = begin; tree < end; ++tree) {
                    writeRootPolicy(out, tree, temperature);
                }
            });
        }
        return out.intoDict();
    }

    void rootPolicyInto(const py::dict& buffers, float temperature = 1.0f) {
        if (!std::isfinite(temperature) || temperature < 0.0f) {
            throw std::invalid_argument("temperature must be finite and non-negative");
        }
        RootBatch out(buffers, trees_.size(), static_cast<size_t>(maxActions_));
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            pool_->parallelFor(trees_.size(), [&](size_t begin, size_t end) {
                for (size_t tree = begin; tree < end; ++tree) {
                    writeRootPolicy(out, tree, temperature);
                }
            });
        }
    }

    py::dict leafBatchSpec(int maxLeaves = 0) const {
        const py::ssize_t rows = static_cast<py::ssize_t>(leafCapacity(maxLeaves));
        const py::ssize_t tiles = static_cast<py::ssize_t>(tileCount());
        const py::ssize_t actions = static_cast<py::ssize_t>(maxActions_);
        py::dict out;
        const auto add = [&](const char* name, const py::dtype& dtype, py::tuple shape) {
            py::dict field;
            field["dtype"] = dtype;
            field["shape"] = std::move(shape);
            out[name] = std::move(field);
        };
        add("map_tokens", py::dtype::of<int32_t>(), py::make_tuple(rows, tiles, kMapTokenFeatureCount));
        add("state", py::dtype::of<int32_t>(), py::make_tuple(rows, kVectorStateFeatureCount));
        add("action_id", py::dtype::of<int32_t>(), py::make_tuple(rows, actions));
        add("action_features", py::dtype::of<int32_t>(), py::make_tuple(rows, actions, kVectorActionFeatureCount));
        add("action_arg_mask", py::dtype::of<uint8_t>(), py::make_tuple(rows, actions, kVectorActionArgMaskCount));
        add("action_mask", py::dtype::of<uint8_t>(), py::make_tuple(rows, actions));
        add("legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("total_legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("action_truncated", py::dtype::of<uint8_t>(), py::make_tuple(rows));
        add("leaf_id", py::dtype::of<uint64_t>(), py::make_tuple(rows));
        add("tree_id", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("to_play", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("player_count", py::dtype::of<int32_t>(), py::make_tuple(rows));
        return out;
    }

    py::dict rootPolicySpec() const {
        const py::ssize_t rows = static_cast<py::ssize_t>(trees_.size());
        const py::ssize_t actions = static_cast<py::ssize_t>(maxActions_);
        py::dict out;
        const auto add = [&](const char* name, const py::dtype& dtype, py::tuple shape) {
            py::dict field;
            field["dtype"] = dtype;
            field["shape"] = std::move(shape);
            out[name] = std::move(field);
        };
        add("action_id", py::dtype::of<int32_t>(), py::make_tuple(rows, actions));
        add("action_mask", py::dtype::of<uint8_t>(), py::make_tuple(rows, actions));
        add("visit_count", py::dtype::of<int32_t>(), py::make_tuple(rows, actions));
        add("policy", py::dtype::of<float>(), py::make_tuple(rows, actions));
        add("root_value", py::dtype::of<float>(), py::make_tuple(rows));
        add("root_visit_count", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("root_player", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("player_count", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("total_legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(rows));
        add("action_truncated", py::dtype::of<uint8_t>(), py::make_tuple(rows));
        return out;
    }

    int numTrees() const { return static_cast<int>(trees_.size()); }
    int numThreads() const { return static_cast<int>(pool_->workerCount()); }
    int maxActions() const { return maxActions_; }
    int mapSize() const { return mapSize_; }
    int playerCount() const { return playerCount_; }
    float cPuct() const { return cPuct_; }
    int pendingCount() const {
        std::lock_guard lock(apiMutex_);
        return static_cast<int>(pending_.size());
    }

private:
    static std::vector<GameEnv> copyRootsFromPython(py::iterable roots) {
        std::vector<GameEnv> copied;
        for (py::handle item : roots) {
            const GameEnv& source = item.cast<const GameEnv&>();
            copied.emplace_back(source.cloneForMcts());
        }
        return copied;
    }

    void validateSettings(int numThreads) const {
        if (maxActions_ <= 0) throw std::invalid_argument("max_actions must be positive");
        if (numThreads < 0) throw std::invalid_argument("num_threads must be non-negative");
        if (!std::isfinite(cPuct_) || cPuct_ <= 0.0f) {
            throw std::invalid_argument("c_puct must be finite and positive");
        }
    }

    struct Node;

    // Edges are expanded eagerly from policy priors, but their GameEnv child
    // state is materialized only when PUCT selects that edge. This is crucial
    // for wide Polytopia action spaces: memory is proportional to visited
    // nodes, not every legal action at every expanded node.
    struct Edge {
        int32_t actionId = -1;
        float prior = 0.0f;
        double valueSum = 0.0;
        uint32_t visits = 0;
        Node* child = nullptr;
    };

    struct Node {
        Node(GameEnv environment, Node* parentNode, Edge* incomingEdge)
            : env(std::move(environment))
            , parent(parentNode)
            , parentEdge(incomingEdge)
            , toPlay(env.currentPlayerNative())
            , terminal(env.isTerminalNative()) {}

        GameEnv env;
        Node* parent = nullptr;
        Edge* parentEdge = nullptr;
        std::vector<Edge> edges;
        double valueSum = 0.0;
        uint32_t visits = 0;
        PlayerId toPlay = kNoPlayer;
        bool expanded = false;
        bool pending = false;
        bool terminal = false;
    };

    struct Tree {
        explicit Tree(GameEnv root) {
            nodes.emplace_back(std::make_unique<Node>(std::move(root), nullptr, nullptr));
        }

        Node& root() { return *nodes.front(); }
        const Node& root() const { return *nodes.front(); }

        std::vector<std::unique_ptr<Node>> nodes;
        uint64_t pendingLeafId = 0;
    };

    // Build a replacement forest before publishing it. This makes a failed
    // belief submission atomic from SelfPlayPool's point of view.
    void initializeRoots(std::vector<GameEnv> roots) {
        if (roots.empty()) throw std::invalid_argument("MctsPool requires at least one GameEnv root");

        int nextMapSize = 0;
        int nextPlayerCount = 0;
        for (const GameEnv& source : roots) {
            const int sourcePlayerCount = source.playerCountNative();
            if (sourcePlayerCount < 2 || sourcePlayerCount > 16) {
                throw std::invalid_argument("MctsPool roots must have between 2 and 16 players");
            }
            if (nextPlayerCount == 0) {
                nextPlayerCount = sourcePlayerCount;
            } else if (sourcePlayerCount != nextPlayerCount) {
                throw std::invalid_argument("all MctsPool roots must have the same player_count");
            }
            if (nextMapSize == 0) {
                nextMapSize = source.mapSizeNative();
            } else if (source.mapSizeNative() != nextMapSize) {
                throw std::invalid_argument("all MctsPool roots must use the same map_size");
            }
            if (source.actionSpaceSize() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("map_size produces action ids too large for MctsPool int32 batches");
            }
        }

        std::vector<Tree> replacement;
        replacement.reserve(roots.size());
        for (GameEnv& root : roots) replacement.emplace_back(std::move(root));
        trees_ = std::move(replacement);
        mapSize_ = nextMapSize;
        playerCount_ = nextPlayerCount;
        twoPlayerZeroSum_ = playerCount_ == 2;
        pending_.clear();
    }

    struct PendingLeaf {
        uint64_t id = 0;
        size_t treeIndex = 0;
        Node* node = nullptr;
    };

    struct Selection {
        uint64_t id = 0;
        size_t treeIndex = 0;
        Node* node = nullptr;
    };

    struct LeafBatch {
        LeafBatch(size_t rows, size_t tiles, size_t maxActions)
            : mapTokens({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(tiles),
                         static_cast<py::ssize_t>(kMapTokenFeatureCount)})
            , state({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(kVectorStateFeatureCount)})
            , actionIds({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)})
            , actionFeatures({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                              static_cast<py::ssize_t>(kVectorActionFeatureCount)})
            , actionArgMasks({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                              static_cast<py::ssize_t>(kVectorActionArgMaskCount)})
            , actionMask({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)})
            , legalActionCount(static_cast<py::ssize_t>(rows))
            , totalLegalActionCount(static_cast<py::ssize_t>(rows))
            , actionTruncated(static_cast<py::ssize_t>(rows))
            , leafId(static_cast<py::ssize_t>(rows))
            , treeId(static_cast<py::ssize_t>(rows))
            , toPlay(static_cast<py::ssize_t>(rows))
            , playerCount(static_cast<py::ssize_t>(rows)) {}

        LeafBatch(const py::dict& buffers, size_t rows, size_t tiles, size_t maxActions)
            : mapTokens(requireWritableBatchArray<int32_t>(
                buffers, "map_tokens", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(tiles),
                                         static_cast<py::ssize_t>(kMapTokenFeatureCount)}))
            , state(requireWritableBatchArray<int32_t>(
                buffers, "state", {static_cast<py::ssize_t>(rows),
                                    static_cast<py::ssize_t>(kVectorStateFeatureCount)}))
            , actionIds(requireWritableBatchArray<int32_t>(
                buffers, "action_id", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)}))
            , actionFeatures(requireWritableBatchArray<int32_t>(
                buffers, "action_features", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                                              static_cast<py::ssize_t>(kVectorActionFeatureCount)}))
            , actionArgMasks(requireWritableBatchArray<uint8_t>(
                buffers, "action_arg_mask", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                                              static_cast<py::ssize_t>(kVectorActionArgMaskCount)}))
            , actionMask(requireWritableBatchArray<uint8_t>(
                buffers, "action_mask", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)}))
            , legalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "legal_action_count", {static_cast<py::ssize_t>(rows)}))
            , totalLegalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "total_legal_action_count", {static_cast<py::ssize_t>(rows)}))
            , actionTruncated(requireWritableBatchArray<uint8_t>(
                buffers, "action_truncated", {static_cast<py::ssize_t>(rows)}))
            , leafId(requireWritableBatchArray<uint64_t>(
                buffers, "leaf_id", {static_cast<py::ssize_t>(rows)}))
            , treeId(requireWritableBatchArray<int32_t>(
                buffers, "tree_id", {static_cast<py::ssize_t>(rows)}))
            , toPlay(requireWritableBatchArray<int32_t>(
                buffers, "to_play", {static_cast<py::ssize_t>(rows)}))
            , playerCount(requireWritableBatchArray<int32_t>(
                buffers, "player_count", {static_cast<py::ssize_t>(rows)}))
        {
            const std::array<std::pair<const char*, const py::array*>, 13> arrays{{
                {"map_tokens", &mapTokens},
                {"state", &state},
                {"action_id", &actionIds},
                {"action_features", &actionFeatures},
                {"action_arg_mask", &actionArgMasks},
                {"action_mask", &actionMask},
                {"legal_action_count", &legalActionCount},
                {"total_legal_action_count", &totalLegalActionCount},
                {"action_truncated", &actionTruncated},
                {"leaf_id", &leafId},
                {"tree_id", &treeId},
                {"to_play", &toPlay},
                {"player_count", &playerCount},
            }};
            validateNonOverlappingBatchStorage(arrays);
        }

        py::dict intoDict() {
            py::dict out;
            out["map_tokens"] = std::move(mapTokens);
            out["state"] = std::move(state);
            out["action_id"] = std::move(actionIds);
            out["action_features"] = std::move(actionFeatures);
            out["action_arg_mask"] = std::move(actionArgMasks);
            out["action_mask"] = std::move(actionMask);
            out["legal_action_count"] = std::move(legalActionCount);
            out["total_legal_action_count"] = std::move(totalLegalActionCount);
            out["action_truncated"] = std::move(actionTruncated);
            out["leaf_id"] = std::move(leafId);
            out["tree_id"] = std::move(treeId);
            out["to_play"] = std::move(toPlay);
            out["player_count"] = std::move(playerCount);
            return out;
        }

        py::array_t<int32_t> mapTokens;
        py::array_t<int32_t> state;
        py::array_t<int32_t> actionIds;
        py::array_t<int32_t> actionFeatures;
        py::array_t<uint8_t> actionArgMasks;
        py::array_t<uint8_t> actionMask;
        py::array_t<int32_t> legalActionCount;
        py::array_t<int32_t> totalLegalActionCount;
        py::array_t<uint8_t> actionTruncated;
        py::array_t<uint64_t> leafId;
        py::array_t<int32_t> treeId;
        py::array_t<int32_t> toPlay;
        py::array_t<int32_t> playerCount;
    };

    struct RootBatch {
        RootBatch(size_t trees, size_t maxActions)
            : actionIds({static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)})
            , actionMask({static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)})
            , visitCounts({static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)})
            , policy({static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)})
            , rootValue(static_cast<py::ssize_t>(trees))
            , rootVisitCount(static_cast<py::ssize_t>(trees))
            , rootPlayer(static_cast<py::ssize_t>(trees))
            , playerCount(static_cast<py::ssize_t>(trees))
            , totalLegalActionCount(static_cast<py::ssize_t>(trees))
            , actionTruncated(static_cast<py::ssize_t>(trees)) {}

        RootBatch(const py::dict& buffers, size_t trees, size_t maxActions)
            : actionIds(requireWritableBatchArray<int32_t>(
                buffers, "action_id", {static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)}))
            , actionMask(requireWritableBatchArray<uint8_t>(
                buffers, "action_mask", {static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)}))
            , visitCounts(requireWritableBatchArray<int32_t>(
                buffers, "visit_count", {static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)}))
            , policy(requireWritableBatchArray<float>(
                buffers, "policy", {static_cast<py::ssize_t>(trees), static_cast<py::ssize_t>(maxActions)}))
            , rootValue(requireWritableBatchArray<float>(
                buffers, "root_value", {static_cast<py::ssize_t>(trees)}))
            , rootVisitCount(requireWritableBatchArray<int32_t>(
                buffers, "root_visit_count", {static_cast<py::ssize_t>(trees)}))
            , rootPlayer(requireWritableBatchArray<int32_t>(
                buffers, "root_player", {static_cast<py::ssize_t>(trees)}))
            , playerCount(requireWritableBatchArray<int32_t>(
                buffers, "player_count", {static_cast<py::ssize_t>(trees)}))
            , totalLegalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "total_legal_action_count", {static_cast<py::ssize_t>(trees)}))
            , actionTruncated(requireWritableBatchArray<uint8_t>(
                buffers, "action_truncated", {static_cast<py::ssize_t>(trees)}))
        {
            const std::array<std::pair<const char*, const py::array*>, 10> arrays{{
                {"action_id", &actionIds},
                {"action_mask", &actionMask},
                {"visit_count", &visitCounts},
                {"policy", &policy},
                {"root_value", &rootValue},
                {"root_visit_count", &rootVisitCount},
                {"root_player", &rootPlayer},
                {"player_count", &playerCount},
                {"total_legal_action_count", &totalLegalActionCount},
                {"action_truncated", &actionTruncated},
            }};
            validateNonOverlappingBatchStorage(arrays);
        }

        py::dict intoDict() {
            py::dict out;
            out["action_id"] = std::move(actionIds);
            out["action_mask"] = std::move(actionMask);
            out["visit_count"] = std::move(visitCounts);
            out["policy"] = std::move(policy);
            out["root_value"] = std::move(rootValue);
            out["root_visit_count"] = std::move(rootVisitCount);
            out["root_player"] = std::move(rootPlayer);
            out["player_count"] = std::move(playerCount);
            out["total_legal_action_count"] = std::move(totalLegalActionCount);
            out["action_truncated"] = std::move(actionTruncated);
            return out;
        }

        py::array_t<int32_t> actionIds;
        py::array_t<uint8_t> actionMask;
        py::array_t<int32_t> visitCounts;
        py::array_t<float> policy;
        py::array_t<float> rootValue;
        py::array_t<int32_t> rootVisitCount;
        py::array_t<int32_t> rootPlayer;
        py::array_t<int32_t> playerCount;
        py::array_t<int32_t> totalLegalActionCount;
        py::array_t<uint8_t> actionTruncated;
    };

    static size_t resolveThreadCount(size_t treeCount, int requested) {
        const unsigned int hardware = std::max(1u, std::thread::hardware_concurrency());
        const size_t wanted = requested > 0 ? static_cast<size_t>(requested) : static_cast<size_t>(hardware);
        return std::max<size_t>(1, std::min(treeCount, wanted));
    }

    size_t tileCount() const {
        return static_cast<size_t>(mapSize_) * static_cast<size_t>(mapSize_);
    }

    size_t leafCapacity(int maxLeaves) const {
        if (maxLeaves < 0) throw std::invalid_argument("max_leaves must be non-negative");
        if (maxLeaves == 0) return trees_.size();
        return std::min(trees_.size(), static_cast<size_t>(maxLeaves));
    }

    // Optimized two-player, zero-sum backup. Keep this scalar/sign-flip path
    // separate from max^n so the common two-player case retains its compact
    // node layout and hot-loop behavior.
    void backupTwoPlayer(Node& leaf, float valueFromLeafPlayer) {
        const PlayerId leafPlayer = leaf.toPlay;
        for (Node* node = &leaf; node; node = node->parent) {
            const float nodeValue = node->toPlay == leafPlayer ? valueFromLeafPlayer : -valueFromLeafPlayer;
            ++node->visits;
            node->valueSum += nodeValue;
            if (node->parentEdge) {
                const float parentValue = node->parent->toPlay == leafPlayer
                    ? valueFromLeafPlayer : -valueFromLeafPlayer;
                ++node->parentEdge->visits;
                node->parentEdge->valueSum += parentValue;
            }
        }
    }

    float multiPlayerValue(const float* values, PlayerId player) const {
        const size_t index = static_cast<size_t>(player);
        if (index >= static_cast<size_t>(playerCount_)) {
            throw std::logic_error("MctsPool encountered an invalid player id while backing up a value vector");
        }
        return values[index];
    }

    // Max^n backup. Node and edge statistics deliberately remain scalar:
    // each node stores its own actor's value, and each incoming edge stores
    // the parent actor's value. That is exactly the Q term selected by PUCT,
    // without an O(player_count) allocation in every node or edge.
    void backupMultiPlayer(Node& leaf, const float* values) {
        for (Node* node = &leaf; node; node = node->parent) {
            ++node->visits;
            node->valueSum += multiPlayerValue(values, node->toPlay);
            if (node->parentEdge) {
                ++node->parentEdge->visits;
                node->parentEdge->valueSum += multiPlayerValue(values, node->parent->toPlay);
            }
        }
    }

    float terminalUtility(PlayerId winner, PlayerId player) const {
        if (winner == kNoPlayer) return 0.0f;
        if (winner == player) return 1.0f;
        // The terminal vector remains zero-sum for every player count, while
        // retaining the historical +1/-1 convention for two players.
        return -1.0f / static_cast<float>(playerCount_ - 1);
    }

    void backupTerminal(Node& leaf) {
        if (twoPlayerZeroSum_) {
            backupTwoPlayer(leaf, leaf.env.terminalValueForCurrentPlayer());
            return;
        }

        const PlayerId winner = leaf.env.terminalWinnerNative();
        for (Node* node = &leaf; node; node = node->parent) {
            ++node->visits;
            node->valueSum += terminalUtility(winner, node->toPlay);
            if (node->parentEdge) {
                ++node->parentEdge->visits;
                node->parentEdge->valueSum += terminalUtility(winner, node->parent->toPlay);
            }
        }
    }

    Node* selectNode(Tree& tree) {
        Node* node = &tree.root();
        while (node->expanded && !node->terminal) {
            Edge* best = nullptr;
            float bestScore = -std::numeric_limits<float>::infinity();
            const float rootVisits = std::sqrt(static_cast<float>(std::max<uint32_t>(1, node->visits)));
            for (Edge& edge : node->edges) {
                const float edgeValue = edge.visits == 0
                    ? 0.0f
                    : static_cast<float>(edge.valueSum / static_cast<double>(edge.visits));
                const float score = edgeValue +
                    cPuct_ * edge.prior * rootVisits / static_cast<float>(1 + edge.visits);
                if (!best || score > bestScore ||
                    (score == bestScore && edge.actionId < best->actionId)) {
                    best = &edge;
                    bestScore = score;
                }
            }
            if (!best) return nullptr;
            if (!best->child) {
                tree.nodes.emplace_back(std::make_unique<Node>(
                    node->env.mctsChild(static_cast<size_t>(best->actionId)), node, best));
                best->child = tree.nodes.back().get();
            }
            node = best->child;
        }

        if (node->terminal || node->env.isTerminalNative()) {
            node->terminal = true;
            backupTerminal(*node);
            return nullptr;
        }
        return node->pending ? nullptr : node;
    }

    std::vector<Selection> selectPendingLeaves(size_t limit) {
        std::vector<Selection> selections;
        selections.reserve(limit);
        if (limit == 0) return selections;

        const size_t treeCount = trees_.size();
        size_t examined = 0;
        try {
            while (examined < treeCount && selections.size() < limit) {
                // Tree traversal can clone a selected child state, which is
                // substantially more expensive than assigning a leaf id. The
                // trees are independent under apiMutex_, so traverse one
                // deterministic wave in parallel and publish its leaves in
                // stable tree order afterwards.
                std::vector<size_t> candidates;
                candidates.reserve(limit - selections.size());
                while (examined < treeCount && candidates.size() < limit - selections.size()) {
                    const size_t treeIndex = (nextTree_ + examined) % treeCount;
                    ++examined;
                    if (trees_[treeIndex].pendingLeafId == 0) candidates.push_back(treeIndex);
                }
                if (candidates.empty()) continue;

                std::vector<Node*> nodes(candidates.size(), nullptr);
                pool_->parallelFor(candidates.size(), [&](size_t begin, size_t end) {
                    for (size_t item = begin; item < end; ++item) {
                        nodes[item] = selectNode(trees_[candidates[item]]);
                    }
                });

                for (size_t item = 0; item < candidates.size(); ++item) {
                    const size_t treeIndex = candidates[item];
                    Tree& tree = trees_[treeIndex];
                    Node* node = nodes[item];
                    if (!node) continue;
                    const GameEnv::ModelActionWindow actions = node->env.modelActionWindow(
                        static_cast<size_t>(maxActions_));
                    if (actions.count == 0) {
                        node->terminal = true;
                        backupTerminal(*node);
                        continue;
                    }

                    const uint64_t id = nextLeafId_++;
                    if (id == 0) throw std::overflow_error("MctsPool leaf id counter overflowed");
                    node->pending = true;
                    tree.pendingLeafId = id;
                    PendingLeaf pending{id, treeIndex, node};
                    pending_.emplace(id, pending);
                    selections.push_back({id, treeIndex, node});
                }
            }
        } catch (...) {
            cancelSelections(selections);
            throw;
        }
        nextTree_ = (nextTree_ + examined) % treeCount;
        return selections;
    }

    void writeLeafBatch(LeafBatch& out, const std::vector<Selection>& selections) {
        const size_t rows = selections.size();
        const size_t actionSlots = rows * static_cast<size_t>(maxActions_);
        std::fill_n(out.actionIds.mutable_data(), actionSlots, int32_t{-1});
        std::fill_n(out.actionFeatures.mutable_data(), actionSlots * kVectorActionFeatureCount, int32_t{0});
        std::fill_n(out.actionArgMasks.mutable_data(), actionSlots * kVectorActionArgMaskCount, uint8_t{0});
        std::fill_n(out.actionMask.mutable_data(), actionSlots, uint8_t{0});

        const size_t tiles = tileCount();
        int32_t* mapData = out.mapTokens.mutable_data();
        int32_t* stateData = out.state.mutable_data();
        int32_t* actionIdData = out.actionIds.mutable_data();
        int32_t* actionFeatureData = out.actionFeatures.mutable_data();
        uint8_t* actionArgMaskData = out.actionArgMasks.mutable_data();
        uint8_t* actionMaskData = out.actionMask.mutable_data();
        int32_t* legalCountData = out.legalActionCount.mutable_data();
        int32_t* totalLegalCountData = out.totalLegalActionCount.mutable_data();
        uint8_t* actionTruncatedData = out.actionTruncated.mutable_data();
        uint64_t* leafIdData = out.leafId.mutable_data();
        int32_t* treeIdData = out.treeId.mutable_data();
        int32_t* toPlayData = out.toPlay.mutable_data();
        int32_t* playerCountData = out.playerCount.mutable_data();

        pool_->parallelFor(rows, [&](size_t begin, size_t end) {
            for (size_t row = begin; row < end; ++row) {
                const Selection& selection = selections[row];
                selection.node->env.writeTrainingPacket(
                    mapData + row * tiles * kMapTokenFeatureCount,
                    stateData + row * kVectorStateFeatureCount,
                    actionIdData + row * static_cast<size_t>(maxActions_),
                    actionFeatureData + row * static_cast<size_t>(maxActions_) * kVectorActionFeatureCount,
                    actionArgMaskData + row * static_cast<size_t>(maxActions_) * kVectorActionArgMaskCount,
                    actionMaskData + row * static_cast<size_t>(maxActions_),
                    legalCountData + row,
                    totalLegalCountData + row,
                    actionTruncatedData + row,
                    static_cast<size_t>(maxActions_),
                    false);
                leafIdData[row] = selection.id;
                treeIdData[row] = static_cast<int32_t>(selection.treeIndex);
                toPlayData[row] = static_cast<int32_t>(selection.node->toPlay);
                playerCountData[row] = playerCount_;
            }
        });
    }

    std::vector<PendingLeaf> resolvePendingLeaves(const uint64_t* ids, size_t count) const {
        std::vector<PendingLeaf> pending;
        pending.reserve(count);
        std::unordered_set<uint64_t> seen;
        seen.reserve(count);
        for (size_t row = 0; row < count; ++row) {
            if (!seen.insert(ids[row]).second) {
                throw std::invalid_argument("leaf_ids must not contain duplicates");
            }
            const auto found = pending_.find(ids[row]);
            if (found == pending_.end()) {
                throw std::invalid_argument("leaf_ids contains an unknown, stale or already-expanded leaf");
            }
            const PendingLeaf& item = found->second;
            if (!item.node || !item.node->pending || trees_[item.treeIndex].pendingLeafId != item.id) {
                throw std::logic_error("MctsPool pending leaf state is inconsistent");
            }
            pending.push_back(item);
        }
        return pending;
    }

    static std::vector<float> softmaxPriors(const float* logits, size_t count) {
        std::vector<float> priors(count, 0.0f);
        float maximum = -std::numeric_limits<float>::infinity();
        for (size_t index = 0; index < count; ++index) {
            if (std::isfinite(logits[index])) maximum = std::max(maximum, logits[index]);
        }
        if (!std::isfinite(maximum)) {
            std::fill(priors.begin(), priors.end(), 1.0f / static_cast<float>(count));
            return priors;
        }

        double sum = 0.0;
        for (size_t index = 0; index < count; ++index) {
            if (!std::isfinite(logits[index])) continue; // permitted -inf
            priors[index] = std::exp(logits[index] - maximum);
            sum += priors[index];
        }
        if (!(sum > 0.0) || !std::isfinite(sum)) {
            std::fill(priors.begin(), priors.end(), 1.0f / static_cast<float>(count));
            return priors;
        }
        for (float& prior : priors) prior = static_cast<float>(prior / sum);
        return priors;
    }

    void expandOne(const PendingLeaf& pending, const float* logits, const float* values) {
        Tree& tree = trees_[pending.treeIndex];
        Node& node = *pending.node;
        if (!node.pending || node.expanded || tree.pendingLeafId != pending.id) {
            throw std::logic_error("MctsPool leaf was modified before expansion");
        }

        const GameEnv::ModelActionWindow actions = node.env.modelActionWindow(
            static_cast<size_t>(maxActions_));
        if (actions.count == 0) {
            node.terminal = true;
            node.pending = false;
            tree.pendingLeafId = 0;
            backupTerminal(node);
            return;
        }
        const std::vector<float> priors = softmaxPriors(logits, actions.count);
        node.edges.clear();
        node.edges.reserve(actions.count);
        for (size_t index = 0; index < actions.count; ++index) {
            node.edges.push_back({static_cast<int32_t>(actions.actionId(index)), priors[index], 0.0, 0, nullptr});
        }
        node.expanded = true;
        node.pending = false;
        tree.pendingLeafId = 0;
        if (twoPlayerZeroSum_) {
            backupTwoPlayer(node, values[0]);
        } else {
            backupMultiPlayer(node, values);
        }
    }

    void clearPending(const std::vector<PendingLeaf>& pending) {
        for (const PendingLeaf& item : pending) {
            auto found = pending_.find(item.id);
            if (found == pending_.end()) continue;
            if (item.node->pending) item.node->pending = false;
            if (trees_[item.treeIndex].pendingLeafId == item.id) {
                trees_[item.treeIndex].pendingLeafId = 0;
            }
            pending_.erase(found);
        }
    }

    void cancelSelections(const std::vector<Selection>& selections) {
        for (const Selection& selection : selections) {
            const auto found = pending_.find(selection.id);
            if (found == pending_.end()) continue;
            if (selection.node->pending) selection.node->pending = false;
            if (trees_[selection.treeIndex].pendingLeafId == selection.id) {
                trees_[selection.treeIndex].pendingLeafId = 0;
            }
            pending_.erase(found);
        }
    }

    // SelfPlayPool uses this only if writing its additional position metadata
    // fails after a native leaf selection. It prevents an internal metadata
    // error from leaving a tree permanently pending.
    void cancelPendingLeafIds(const uint64_t* ids, size_t count) {
        std::lock_guard lock(apiMutex_);
        for (size_t row = 0; row < count; ++row) {
            const auto found = pending_.find(ids[row]);
            if (found == pending_.end()) continue;
            const PendingLeaf& item = found->second;
            if (item.node && item.node->pending) item.node->pending = false;
            if (trees_[item.treeIndex].pendingLeafId == item.id) {
                trees_[item.treeIndex].pendingLeafId = 0;
            }
            pending_.erase(found);
        }
    }

    void writeRootPolicy(RootBatch& out, size_t treeIndex, float temperature) const {
        const Node& root = trees_[treeIndex].root();
        const size_t offset = treeIndex * static_cast<size_t>(maxActions_);
        int32_t* actionIds = out.actionIds.mutable_data() + offset;
        uint8_t* actionMask = out.actionMask.mutable_data() + offset;
        int32_t* visitCounts = out.visitCounts.mutable_data() + offset;
        float* policy = out.policy.mutable_data() + offset;
        std::fill_n(actionIds, static_cast<size_t>(maxActions_), int32_t{-1});
        std::fill_n(actionMask, static_cast<size_t>(maxActions_), uint8_t{0});
        std::fill_n(visitCounts, static_cast<size_t>(maxActions_), int32_t{0});
        std::fill_n(policy, static_cast<size_t>(maxActions_), 0.0f);
        out.rootValue.mutable_data()[treeIndex] = root.visits == 0
            ? 0.0f : static_cast<float>(root.valueSum / static_cast<double>(root.visits));
        out.rootVisitCount.mutable_data()[treeIndex] = root.visits > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            ? std::numeric_limits<int32_t>::max() : static_cast<int32_t>(root.visits);
        out.rootPlayer.mutable_data()[treeIndex] = static_cast<int32_t>(root.toPlay);
        out.playerCount.mutable_data()[treeIndex] = playerCount_;
        const GameEnv::ModelActionWindow rootActions = root.env.modelActionWindow(
            static_cast<size_t>(maxActions_));
        out.totalLegalActionCount.mutable_data()[treeIndex] = static_cast<int32_t>(rootActions.totalCount);
        out.actionTruncated.mutable_data()[treeIndex] = rootActions.truncated() ? 1 : 0;

        if (!root.expanded) return;
        const size_t count = root.edges.size();
        if (count != rootActions.count) {
            throw std::logic_error("MctsPool root action window is inconsistent with its expanded edges");
        }
        double weightSum = 0.0;
        std::vector<double> weights(count, 0.0);
        if (temperature == 0.0f) {
            size_t best = 0;
            for (size_t index = 1; index < count; ++index) {
                const Edge& candidate = root.edges[index];
                const Edge& current = root.edges[best];
                if (candidate.visits > current.visits ||
                    (candidate.visits == current.visits && candidate.prior > current.prior) ||
                    (candidate.visits == current.visits && candidate.prior == current.prior &&
                     candidate.actionId < current.actionId)) {
                    best = index;
                }
            }
            weights[best] = 1.0;
            weightSum = 1.0;
        } else {
            const double exponent = 1.0 / static_cast<double>(temperature);
            for (size_t index = 0; index < count; ++index) {
                const uint32_t visits = root.edges[index].visits;
                if (visits > 0) weights[index] = std::pow(static_cast<double>(visits), exponent);
                weightSum += weights[index];
            }
            if (weightSum == 0.0) {
                for (size_t index = 0; index < count; ++index) {
                    weights[index] = root.edges[index].prior;
                    weightSum += weights[index];
                }
            }
        }
        for (size_t index = 0; index < count; ++index) {
            const Edge& edge = root.edges[index];
            actionIds[index] = edge.actionId;
            actionMask[index] = 1;
            visitCounts[index] = edge.visits > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                ? std::numeric_limits<int32_t>::max() : static_cast<int32_t>(edge.visits);
            policy[index] = weightSum > 0.0 ? static_cast<float>(weights[index] / weightSum) : 0.0f;
        }
    }

    int mapSize_ = 0;
    int playerCount_ = 0;
    int maxActions_ = 0;
    float cPuct_ = 1.5f;
    bool twoPlayerZeroSum_ = true;
    std::vector<Tree> trees_;
    std::unordered_map<uint64_t, PendingLeaf> pending_;
    uint64_t nextLeafId_ = 1;
    size_t nextTree_ = 0;
    std::unique_ptr<BatchThreadPool> ownedPool_;
    BatchThreadPool* pool_ = nullptr;
    mutable std::mutex apiMutex_;
};

// A native self-play scheduler for an external policy/value implementation.
// It owns authoritative live games and a separate forest of belief roots. The
// two are intentionally never exposed as Python GameEnv objects: Python sees
// only dense player-view packets and returns model predictions / hypotheses.
class SelfPlayPool {
public:
    SelfPlayPool(int numEnvs,
                 int mapSize,
                 const std::vector<int>& tribes,
                 uint32_t seed,
                 const std::string& unitsJsonPath,
                 MapType mapType,
                 int numThreads,
                 int maxActions,
                 bool autoReset,
                 float cPuct)
        : numEnvs_(numEnvs)
        , mapSize_(mapSize)
        , tribes_(tribes.empty() ? std::vector<int>{3, 2} : tribes)
        , baseSeed_(seed)
        , unitsJsonPath_(unitsJsonPath.empty() ? resolveDefaultUnitsJsonPath() : unitsJsonPath)
        , mapType_(mapType)
        , maxActions_(maxActions)
        , autoReset_(autoReset)
        , cPuct_(cPuct)
        , pool_(resolveThreadCount(numEnvs, numThreads))
    {
        if (numEnvs_ <= 0) throw std::invalid_argument("num_envs must be positive");
        if (mapSize_ <= 0) throw std::invalid_argument("map_size must be positive");
        if (maxActions_ <= 0) throw std::invalid_argument("max_actions must be positive");
        if (numThreads < 0) throw std::invalid_argument("num_threads must be non-negative");
        if (tribes_.size() < 2 || tribes_.size() > 16) {
            throw std::invalid_argument("SelfPlayPool requires between 2 and 16 players");
        }
        playerCount_ = static_cast<int>(tribes_.size());
        if (!std::isfinite(cPuct_) || cPuct_ <= 0.0f) {
            throw std::invalid_argument("c_puct must be finite and positive");
        }

        // Unit definitions are immutable during native execution. Load them
        // before this pool's workers can touch a live environment.
        GameDataSystem::loadUnits(unitsJsonPath_);
        envs_.reserve(static_cast<size_t>(numEnvs_));
        resetCounts_.assign(static_cast<size_t>(numEnvs_), 0);
        stateGenerations_.assign(static_cast<size_t>(numEnvs_), 0);
        episodeGenerations_.assign(static_cast<size_t>(numEnvs_), 0);
        stateIds_.assign(static_cast<size_t>(numEnvs_), 0);
        episodeIds_.assign(static_cast<size_t>(numEnvs_), 0);
        beliefValidationScratch_.resize(
            static_cast<size_t>(numEnvs_) * tileCount() * kMapTokenFeatureCount);
        for (int i = 0; i < numEnvs_; ++i) {
            envs_.emplace_back(mapSize_, tribes_, seedFor(static_cast<size_t>(i)), unitsJsonPath_, mapType_);
            beginEpisode(static_cast<size_t>(i));
            advanceState(static_cast<size_t>(i));
        }
        if (envs_.front().actionSpaceSize() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            throw std::invalid_argument("map_size produces action ids too large for SelfPlayPool int32 batches");
        }

        // These roots are inert until submit_beliefs(). They exist only so the
        // shared native scheduler and its permanent data structures can be
        // created once, instead of allocating worker threads on every move.
        mcts_ = std::make_unique<MctsPool>(makeDormantRoots(), pool_, maxActions_, cPuct_);
    }

    SelfPlayPool(const SelfPlayPool&) = delete;
    SelfPlayPool& operator=(const SelfPlayPool&) = delete;

    // Reset all authoritative games and return only their current players'
    // observations. state_id is an opaque, per-slot position token required
    // by submit_beliefs(), preventing late model completions from being used.
    py::dict reset(std::optional<uint32_t> seed = std::nullopt) {
        BeliefRequestBatch out(static_cast<size_t>(numEnvs_), tileCount(), static_cast<size_t>(maxActions_), playerCount_);
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            if (seed) {
                baseSeed_ = *seed;
                std::fill(resetCounts_.begin(), resetCounts_.end(), 0);
            }
            // Invalidate Python-visible search handles before any live slot
            // changes, including the unlikely case that reset encoding throws.
            searchActive_ = false;
            for (size_t i = 0; i < envs_.size(); ++i) {
                resetOne(i);
                beginEpisode(i);
                advanceState(i);
            }
            // A reset invalidates outstanding leaf ids. replaceRoots keeps the
            // worker pool alive while clearing its pending-id table.
            mcts_->replaceRoots(makeDormantRoots());
            writeBeliefRequests(out, nullptr, false);
        }
        return out.intoDict();
    }

    // Caller-owned equivalent of reset(). The external model may supply NumPy
    // views of its pinned host tensors, but this class remains unaware of any
    // CUDA stream/event lifetime after the synchronous write returns.
    void resetInto(const py::dict& buffers, std::optional<uint32_t> seed = std::nullopt) {
        BeliefRequestBatch out(buffers, static_cast<size_t>(numEnvs_), tileCount(),
                               static_cast<size_t>(maxActions_), playerCount_);
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            if (seed) {
                baseSeed_ = *seed;
                std::fill(resetCounts_.begin(), resetCounts_.end(), 0);
            }
            searchActive_ = false;
            for (size_t i = 0; i < envs_.size(); ++i) {
                resetOne(i);
                beginEpisode(i);
                advanceState(i);
            }
            mcts_->replaceRoots(makeDormantRoots());
            writeBeliefRequests(out, nullptr, false);
        }
    }

    // Return a fresh, immutable NumPy batch of public observation data for
    // the current positions. This is useful when an external belief model is
    // scheduled separately from the game loop.
    py::dict beliefRequests() {
        BeliefRequestBatch out(static_cast<size_t>(numEnvs_), tileCount(), static_cast<size_t>(maxActions_), playerCount_);
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            writeBeliefRequests(out, nullptr, false);
        }
        return out.intoDict();
    }

    void beliefRequestsInto(const py::dict& buffers) {
        BeliefRequestBatch out(buffers, static_cast<size_t>(numEnvs_), tileCount(),
                               static_cast<size_t>(maxActions_), playerCount_);
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            writeBeliefRequests(out, nullptr, false);
        }
    }

    // Build one detached root belief per authoritative game. The tensor must
    // already be contiguous int32 to keep the hot path allocation-free on the
    // Python side. The C++ builder validates every revealed tile before it
    // reads hidden hypotheses and never clones a live hidden world.
    void submitBeliefs(py::array stateIds, py::array completedMapTokens) {
        auto ids = py::array_t<uint64_t, py::array::c_style>::ensure(stateIds);
        auto tokens = py::array_t<int32_t, py::array::c_style>::ensure(completedMapTokens);
        if (!ids || ids.ndim() != 1 || ids.shape(0) != numEnvs_) {
            throw std::invalid_argument("state_ids must be a contiguous uint64 array with one entry per environment");
        }
        if (!tokens || tokens.ndim() != 3 || tokens.shape(0) != numEnvs_ ||
            tokens.shape(1) != static_cast<py::ssize_t>(tileCount()) ||
            tokens.shape(2) != static_cast<py::ssize_t>(kMapTokenFeatureCount)) {
            throw std::invalid_argument(
                "completed_map_tokens must be a contiguous int32 array with shape [num_envs, map_size * map_size, 23]");
        }

        const uint64_t* idData = ids.data();
        const int32_t* tokenData = tokens.data();
        const size_t tiles = tileCount();
        const size_t tokenStride = tiles * kMapTokenFeatureCount;
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            if (searchActive_) {
                throw std::logic_error("a belief forest is already active; step or reset before submitting new beliefs");
            }
            for (size_t i = 0; i < envs_.size(); ++i) {
                if (idData[i] != stateIds_[i]) {
                    throw std::invalid_argument("state_ids contains a stale or mismatched self-play position id");
                }
            }

            // Keep completed roots in per-slot optionals so workers never
            // contend on a vector push_back. Nothing is committed to MCTS
            // until every belief has built and passed its public-action check.
            std::vector<std::optional<GameEnv>> built(envs_.size());
            pool_.parallelFor(envs_.size(), [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    GameEnv belief = envs_[i].makeBeliefEnvFromCurrentPlayerFlatTokens(
                        tokenData + i * tokenStride,
                        tiles,
                        kMapTokenFeatureCount,
                        beliefValidationScratch_.data() + i * tokenStride);
                    // A hallucinated hidden tile must never create a root move
                    // unavailable in the real position. Reject it rather than
                    // silently falling back to authoritative hidden state.
                    if (!envs_[i].hasSameCurrentLegalActions(belief)) {
                        throw std::invalid_argument(
                            "completed_map_tokens changes the current player's legal action set");
                    }
                    built[i].emplace(std::move(belief));
                }
            });

            std::vector<GameEnv> roots;
            roots.reserve(built.size());
            for (std::optional<GameEnv>& root : built) {
                if (!root) throw std::logic_error("belief build completed without a root");
                roots.emplace_back(std::move(*root));
            }
            mcts_->resetRoots(std::move(roots));
            searchActive_ = true;
        }
    }

    py::dict selectLeaves(int maxLeaves = 0) {
        std::lock_guard lock(apiMutex_);
        requireActiveSearch();
        py::dict out = mcts_->selectLeaves(maxLeaves);
        attachLeafPositionMetadata(out);
        return out;
    }

    // Fixed-capacity caller-owned MCTS leaf batch. Only rows
    // [0:returned_leaf_count] are current; the remainder is deliberately
    // untouched so the caller can reuse a pinned slot without a large clear.
    int selectLeavesInto(const py::dict& buffers, int maxLeaves = 0) {
        const size_t capacity = leafCapacity(maxLeaves);
        MctsPool::LeafBatch leaf(buffers, capacity, tileCount(), static_cast<size_t>(maxActions_));
        LeafPositionBatch position(buffers, capacity);
        validateLeafBatchStorage(leaf, position);

        std::lock_guard lock(apiMutex_);
        requireActiveSearch();
        const int count = mcts_->selectLeavesInto(buffers, maxLeaves);
        try {
            attachLeafPositionMetadata(leaf, position, static_cast<size_t>(count));
        } catch (...) {
            mcts_->cancelPendingLeafIds(leaf.leafId.data(), static_cast<size_t>(count));
            throw;
        }
        return count;
    }

    void expandAndBackup(py::array leafIds, py::array policyLogits, py::array values) {
        std::lock_guard lock(apiMutex_);
        requireActiveSearch();
        mcts_->expandAndBackup(leafIds, policyLogits, values);
    }

    py::dict rootPolicy(float temperature = 1.0f) {
        std::lock_guard lock(apiMutex_);
        requireActiveSearch();
        py::dict out = mcts_->rootPolicy(temperature);
        attachRootPositionMetadata(out);
        return out;
    }

    void rootPolicyInto(const py::dict& buffers, float temperature = 1.0f) {
        MctsPool::RootBatch root(buffers, static_cast<size_t>(numEnvs_), static_cast<size_t>(maxActions_));
        RootPositionBatch position(buffers, static_cast<size_t>(numEnvs_));
        validateRootBatchStorage(root, position);

        std::lock_guard lock(apiMutex_);
        requireActiveSearch();
        mcts_->rootPolicyInto(buffers, temperature);
        attachRootPositionMetadata(position);
    }

    py::dict leafBatchSpec(int maxLeaves = 0) const {
        const size_t capacity = leafCapacity(maxLeaves);
        py::dict out = mcts_->leafBatchSpec(maxLeaves);
        const auto add = [&](const char* name, const py::dtype& dtype) {
            py::dict field;
            field["dtype"] = dtype;
            field["shape"] = py::make_tuple(static_cast<py::ssize_t>(capacity));
            out[name] = std::move(field);
        };
        add("env_id", py::dtype::of<int32_t>());
        add("state_id", py::dtype::of<uint64_t>());
        add("episode_id", py::dtype::of<uint64_t>());
        return out;
    }

    py::dict rootPolicySpec() const {
        py::dict out = mcts_->rootPolicySpec();
        const auto add = [&](const char* name, const py::dtype& dtype) {
            py::dict field;
            field["dtype"] = dtype;
            field["shape"] = py::make_tuple(static_cast<py::ssize_t>(numEnvs_));
            out[name] = std::move(field);
        };
        add("env_id", py::dtype::of<int32_t>());
        add("state_id", py::dtype::of<uint64_t>());
        add("episode_id", py::dtype::of<uint64_t>());
        return out;
    }

    // Execute a selected action in every authoritative game. It is legal to
    // inspect a partial root policy while leaves are pending, but not to
    // advance: that would make a late GPU answer refer to the wrong tree.
    py::dict step(py::array actionIds) {
        auto actions = py::array_t<int32_t, py::array::c_style | py::array::forcecast>::ensure(actionIds);
        if (!actions || actions.ndim() != 1 || actions.shape(0) != numEnvs_) {
            throw std::invalid_argument("action_ids must be a contiguous one-dimensional int32-compatible array with num_envs entries");
        }

        BeliefRequestBatch out(static_cast<size_t>(numEnvs_), tileCount(), static_cast<size_t>(maxActions_), playerCount_);
        const int32_t* actionData = actions.data();
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            requireActiveSearch();
            if (mcts_->pendingCount() != 0) {
                throw std::logic_error("cannot step SelfPlayPool while MCTS leaf evaluations are pending");
            }
            try {
                writeBeliefRequests(out, actionData, true);
            } catch (...) {
                // A live step may already have succeeded before an output
                // capacity/encoding error is reported. Never leave the old
                // forest callable for that new live position.
                searchActive_ = false;
                throw;
            }
            // Old roots are deliberately not reused after a real move: a new
            // external belief hypothesis may differ, especially under fog.
            // Their ids are already fully resolved, so no extra cloning is
            // required until the next submit_beliefs().
            searchActive_ = false;
        }
        return out.intoDict();
    }

    void stepInto(py::array actionIds, const py::dict& buffers) {
        if (!actionIds.dtype().is(py::dtype::of<int32_t>()) ||
            actionIds.ndim() != 1 || actionIds.shape(0) != numEnvs_) {
            throw std::invalid_argument(
                "action_ids for step_into must be a one-dimensional int32 array with num_envs entries");
        }
        const auto actions = py::reinterpret_borrow<py::array_t<int32_t>>(actionIds);
        // Validate all output fields, then detach the potentially aliased
        // input vector before GIL release and before output is overwritten.
        BeliefRequestBatch out(buffers, static_cast<size_t>(numEnvs_), tileCount(),
                               static_cast<size_t>(maxActions_), playerCount_);
        std::vector<int32_t> copiedActions(static_cast<size_t>(numEnvs_));
        const auto actionView = actions.unchecked<1>();
        for (py::ssize_t i = 0; i < actionView.shape(0); ++i) {
            copiedActions[static_cast<size_t>(i)] = actionView(i);
        }
        {
            py::gil_scoped_release release;
            std::lock_guard lock(apiMutex_);
            requireActiveSearch();
            if (mcts_->pendingCount() != 0) {
                throw std::logic_error("cannot step SelfPlayPool while MCTS leaf evaluations are pending");
            }
            try {
                writeBeliefRequests(out, copiedActions.data(), true);
            } catch (...) {
                searchActive_ = false;
                throw;
            }
            searchActive_ = false;
        }
    }

    py::dict beliefBatchSpec() const {
        const py::ssize_t batch = static_cast<py::ssize_t>(numEnvs_);
        const py::ssize_t tiles = static_cast<py::ssize_t>(tileCount());
        const py::ssize_t actions = static_cast<py::ssize_t>(maxActions_);
        const py::ssize_t players = static_cast<py::ssize_t>(playerCount_);
        py::dict out;
        const auto add = [&](const char* name, const py::dtype& dtype, py::tuple shape) {
            py::dict field;
            field["dtype"] = dtype;
            field["shape"] = std::move(shape);
            out[name] = std::move(field);
        };
        add("map_tokens", py::dtype::of<int32_t>(), py::make_tuple(batch, tiles, kMapTokenFeatureCount));
        add("state", py::dtype::of<int32_t>(), py::make_tuple(batch, kVectorStateFeatureCount));
        add("action_id", py::dtype::of<int32_t>(), py::make_tuple(batch, actions));
        add("action_features", py::dtype::of<int32_t>(), py::make_tuple(batch, actions, kVectorActionFeatureCount));
        add("action_arg_mask", py::dtype::of<uint8_t>(), py::make_tuple(batch, actions, kVectorActionArgMaskCount));
        add("action_mask", py::dtype::of<uint8_t>(), py::make_tuple(batch, actions));
        add("legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("total_legal_action_count", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("action_truncated", py::dtype::of<uint8_t>(), py::make_tuple(batch));
        add("reward", py::dtype::of<float>(), py::make_tuple(batch));
        add("terminated", py::dtype::of<uint8_t>(), py::make_tuple(batch));
        add("action_valid", py::dtype::of<uint8_t>(), py::make_tuple(batch));
        add("winner", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("terminal_values", py::dtype::of<float>(), py::make_tuple(batch, players));
        add("env_id", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("to_play", py::dtype::of<int32_t>(), py::make_tuple(batch));
        add("state_id", py::dtype::of<uint64_t>(), py::make_tuple(batch));
        add("episode_id", py::dtype::of<uint64_t>(), py::make_tuple(batch));
        return out;
    }

    int numEnvs() const { return numEnvs_; }
    int numThreads() const { return static_cast<int>(pool_.workerCount()); }
    int mapSize() const { return mapSize_; }
    int playerCount() const { return playerCount_; }
    int maxActions() const { return maxActions_; }
    bool autoReset() const { return autoReset_; }
    float cPuct() const { return cPuct_; }
    bool searchActive() const {
        std::lock_guard lock(apiMutex_);
        return searchActive_;
    }
    int pendingCount() const {
        std::lock_guard lock(apiMutex_);
        return mcts_->pendingCount();
    }

    std::vector<std::string> actionFeatureNames() const {
        return {
            "type_id", "source_index", "target_index", "city", "tech", "building",
            "spawn_type", "upgrade", "tile_action", "unit_upgrade", "unit_id",
            "population_gain", "cost_stars", "stars_before", "affordable",
            "damage_dealt", "damage_received",
        };
    }

    std::vector<std::string> stateFeatureNames() const {
        return {
            "turn", "current_player", "game_over", "winner", "player_stars",
            "owns_units", "own_cities", "next_turn_star_income", "pending_city_id",
            "pending_upgrade_a", "pending_upgrade_b",
        };
    }

private:
    struct LeafPositionBatch {
        LeafPositionBatch(const py::dict& buffers, size_t rows)
            : envId(requireWritableBatchArray<int32_t>(
                buffers, "env_id", {static_cast<py::ssize_t>(rows)}))
            , stateId(requireWritableBatchArray<uint64_t>(
                buffers, "state_id", {static_cast<py::ssize_t>(rows)}))
            , episodeId(requireWritableBatchArray<uint64_t>(
                buffers, "episode_id", {static_cast<py::ssize_t>(rows)})) {}

        py::array_t<int32_t> envId;
        py::array_t<uint64_t> stateId;
        py::array_t<uint64_t> episodeId;
    };

    struct RootPositionBatch {
        RootPositionBatch(const py::dict& buffers, size_t rows)
            : envId(requireWritableBatchArray<int32_t>(
                buffers, "env_id", {static_cast<py::ssize_t>(rows)}))
            , stateId(requireWritableBatchArray<uint64_t>(
                buffers, "state_id", {static_cast<py::ssize_t>(rows)}))
            , episodeId(requireWritableBatchArray<uint64_t>(
                buffers, "episode_id", {static_cast<py::ssize_t>(rows)})) {}

        py::array_t<int32_t> envId;
        py::array_t<uint64_t> stateId;
        py::array_t<uint64_t> episodeId;
    };

    struct BeliefRequestBatch {
        BeliefRequestBatch(size_t rows, size_t tiles, size_t maxActions, int playerCount)
            : mapTokens({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(tiles),
                         static_cast<py::ssize_t>(kMapTokenFeatureCount)})
            , state({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(kVectorStateFeatureCount)})
            , actionIds({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)})
            , actionFeatures({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                              static_cast<py::ssize_t>(kVectorActionFeatureCount)})
            , actionArgMasks({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                              static_cast<py::ssize_t>(kVectorActionArgMaskCount)})
            , actionMask({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)})
            , legalActionCount(static_cast<py::ssize_t>(rows))
            , totalLegalActionCount(static_cast<py::ssize_t>(rows))
            , actionTruncated(static_cast<py::ssize_t>(rows))
            , reward(static_cast<py::ssize_t>(rows))
            , terminated(static_cast<py::ssize_t>(rows))
            , actionValid(static_cast<py::ssize_t>(rows))
            , winner(static_cast<py::ssize_t>(rows))
            , terminalValues({static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(playerCount)})
            , envId(static_cast<py::ssize_t>(rows))
            , toPlay(static_cast<py::ssize_t>(rows))
            , stateId(static_cast<py::ssize_t>(rows))
            , episodeId(static_cast<py::ssize_t>(rows)) {}

        BeliefRequestBatch(const py::dict& buffers, size_t rows, size_t tiles,
                           size_t maxActions, int playerCount)
            : mapTokens(requireWritableBatchArray<int32_t>(
                buffers, "map_tokens", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(tiles),
                                         static_cast<py::ssize_t>(kMapTokenFeatureCount)}))
            , state(requireWritableBatchArray<int32_t>(
                buffers, "state", {static_cast<py::ssize_t>(rows),
                                    static_cast<py::ssize_t>(kVectorStateFeatureCount)}))
            , actionIds(requireWritableBatchArray<int32_t>(
                buffers, "action_id", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)}))
            , actionFeatures(requireWritableBatchArray<int32_t>(
                buffers, "action_features", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                                              static_cast<py::ssize_t>(kVectorActionFeatureCount)}))
            , actionArgMasks(requireWritableBatchArray<uint8_t>(
                buffers, "action_arg_mask", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions),
                                              static_cast<py::ssize_t>(kVectorActionArgMaskCount)}))
            , actionMask(requireWritableBatchArray<uint8_t>(
                buffers, "action_mask", {static_cast<py::ssize_t>(rows), static_cast<py::ssize_t>(maxActions)}))
            , legalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "legal_action_count", {static_cast<py::ssize_t>(rows)}))
            , totalLegalActionCount(requireWritableBatchArray<int32_t>(
                buffers, "total_legal_action_count", {static_cast<py::ssize_t>(rows)}))
            , actionTruncated(requireWritableBatchArray<uint8_t>(
                buffers, "action_truncated", {static_cast<py::ssize_t>(rows)}))
            , reward(requireWritableBatchArray<float>(
                buffers, "reward", {static_cast<py::ssize_t>(rows)}))
            , terminated(requireWritableBatchArray<uint8_t>(
                buffers, "terminated", {static_cast<py::ssize_t>(rows)}))
            , actionValid(requireWritableBatchArray<uint8_t>(
                buffers, "action_valid", {static_cast<py::ssize_t>(rows)}))
            , winner(requireWritableBatchArray<int32_t>(
                buffers, "winner", {static_cast<py::ssize_t>(rows)}))
            , terminalValues(requireWritableBatchArray<float>(
                buffers, "terminal_values", {static_cast<py::ssize_t>(rows),
                                               static_cast<py::ssize_t>(playerCount)}))
            , envId(requireWritableBatchArray<int32_t>(
                buffers, "env_id", {static_cast<py::ssize_t>(rows)}))
            , toPlay(requireWritableBatchArray<int32_t>(
                buffers, "to_play", {static_cast<py::ssize_t>(rows)}))
            , stateId(requireWritableBatchArray<uint64_t>(
                buffers, "state_id", {static_cast<py::ssize_t>(rows)}))
            , episodeId(requireWritableBatchArray<uint64_t>(
                buffers, "episode_id", {static_cast<py::ssize_t>(rows)}))
        {
            const std::array<std::pair<const char*, const py::array*>, 18> arrays{{
                {"map_tokens", &mapTokens},
                {"state", &state},
                {"action_id", &actionIds},
                {"action_features", &actionFeatures},
                {"action_arg_mask", &actionArgMasks},
                {"action_mask", &actionMask},
                {"legal_action_count", &legalActionCount},
                {"total_legal_action_count", &totalLegalActionCount},
                {"action_truncated", &actionTruncated},
                {"reward", &reward},
                {"terminated", &terminated},
                {"action_valid", &actionValid},
                {"winner", &winner},
                {"terminal_values", &terminalValues},
                {"env_id", &envId},
                {"to_play", &toPlay},
                {"state_id", &stateId},
                {"episode_id", &episodeId},
            }};
            validateNonOverlappingBatchStorage(arrays);
        }

        py::dict intoDict() {
            py::dict out;
            out["map_tokens"] = std::move(mapTokens);
            out["state"] = std::move(state);
            out["action_id"] = std::move(actionIds);
            out["action_features"] = std::move(actionFeatures);
            out["action_arg_mask"] = std::move(actionArgMasks);
            out["action_mask"] = std::move(actionMask);
            out["legal_action_count"] = std::move(legalActionCount);
            out["total_legal_action_count"] = std::move(totalLegalActionCount);
            out["action_truncated"] = std::move(actionTruncated);
            out["reward"] = std::move(reward);
            out["terminated"] = std::move(terminated);
            out["action_valid"] = std::move(actionValid);
            out["winner"] = std::move(winner);
            out["terminal_values"] = std::move(terminalValues);
            out["env_id"] = std::move(envId);
            out["to_play"] = std::move(toPlay);
            out["state_id"] = std::move(stateId);
            out["episode_id"] = std::move(episodeId);
            return out;
        }

        py::array_t<int32_t> mapTokens;
        py::array_t<int32_t> state;
        py::array_t<int32_t> actionIds;
        py::array_t<int32_t> actionFeatures;
        py::array_t<uint8_t> actionArgMasks;
        py::array_t<uint8_t> actionMask;
        py::array_t<int32_t> legalActionCount;
        py::array_t<int32_t> totalLegalActionCount;
        py::array_t<uint8_t> actionTruncated;
        py::array_t<float> reward;
        py::array_t<uint8_t> terminated;
        py::array_t<uint8_t> actionValid;
        py::array_t<int32_t> winner;
        py::array_t<float> terminalValues;
        py::array_t<int32_t> envId;
        py::array_t<int32_t> toPlay;
        py::array_t<uint64_t> stateId;
        py::array_t<uint64_t> episodeId;
    };

    static void validateLeafBatchStorage(
        const MctsPool::LeafBatch& leaf,
        const LeafPositionBatch& position)
    {
        const std::array<std::pair<const char*, const py::array*>, 16> arrays{{
            {"map_tokens", &leaf.mapTokens},
            {"state", &leaf.state},
            {"action_id", &leaf.actionIds},
            {"action_features", &leaf.actionFeatures},
            {"action_arg_mask", &leaf.actionArgMasks},
            {"action_mask", &leaf.actionMask},
            {"legal_action_count", &leaf.legalActionCount},
            {"total_legal_action_count", &leaf.totalLegalActionCount},
            {"action_truncated", &leaf.actionTruncated},
            {"leaf_id", &leaf.leafId},
            {"tree_id", &leaf.treeId},
            {"to_play", &leaf.toPlay},
            {"player_count", &leaf.playerCount},
            {"env_id", &position.envId},
            {"state_id", &position.stateId},
            {"episode_id", &position.episodeId},
        }};
        validateNonOverlappingBatchStorage(arrays);
    }

    static void validateRootBatchStorage(
        const MctsPool::RootBatch& root,
        const RootPositionBatch& position)
    {
        const std::array<std::pair<const char*, const py::array*>, 13> arrays{{
            {"action_id", &root.actionIds},
            {"action_mask", &root.actionMask},
            {"visit_count", &root.visitCounts},
            {"policy", &root.policy},
            {"root_value", &root.rootValue},
            {"root_visit_count", &root.rootVisitCount},
            {"root_player", &root.rootPlayer},
            {"player_count", &root.playerCount},
            {"total_legal_action_count", &root.totalLegalActionCount},
            {"action_truncated", &root.actionTruncated},
            {"env_id", &position.envId},
            {"state_id", &position.stateId},
            {"episode_id", &position.episodeId},
        }};
        validateNonOverlappingBatchStorage(arrays);
    }

    void attachLeafPositionMetadata(
        const MctsPool::LeafBatch& leaf,
        LeafPositionBatch& position,
        size_t rows) const
    {
        const int32_t* treeData = leaf.treeId.data();
        int32_t* envData = position.envId.mutable_data();
        uint64_t* stateData = position.stateId.mutable_data();
        uint64_t* episodeData = position.episodeId.mutable_data();
        for (size_t row = 0; row < rows; ++row) {
            const int32_t tree = treeData[row];
            if (tree < 0 || tree >= numEnvs_) {
                throw std::logic_error("MCTS returned an out-of-range tree id");
            }
            const size_t env = static_cast<size_t>(tree);
            envData[row] = tree;
            stateData[row] = stateIds_[env];
            episodeData[row] = episodeIds_[env];
        }
    }

    void attachRootPositionMetadata(RootPositionBatch& position) const {
        int32_t* envData = position.envId.mutable_data();
        uint64_t* stateData = position.stateId.mutable_data();
        uint64_t* episodeData = position.episodeId.mutable_data();
        for (size_t i = 0; i < envs_.size(); ++i) {
            envData[i] = static_cast<int32_t>(i);
            stateData[i] = stateIds_[i];
            episodeData[i] = episodeIds_[i];
        }
    }

    static size_t resolveThreadCount(int numEnvs, int requested) {
        const unsigned int hardware = std::max(1u, std::thread::hardware_concurrency());
        const size_t wanted = requested > 0 ? static_cast<size_t>(requested) : static_cast<size_t>(hardware);
        return std::max<size_t>(1, std::min(static_cast<size_t>(std::max(1, numEnvs)), wanted));
    }

    size_t tileCount() const {
        return static_cast<size_t>(mapSize_) * static_cast<size_t>(mapSize_);
    }

    size_t leafCapacity(int maxLeaves) const {
        if (maxLeaves < 0) throw std::invalid_argument("max_leaves must be non-negative");
        if (maxLeaves == 0) return static_cast<size_t>(numEnvs_);
        return std::min(static_cast<size_t>(numEnvs_), static_cast<size_t>(maxLeaves));
    }

    uint32_t seedFor(size_t envIndex) const {
        if (baseSeed_ == 0) return 0;
        return baseSeed_ + static_cast<uint32_t>(envIndex) +
            static_cast<uint32_t>(resetCounts_[envIndex] * static_cast<uint64_t>(numEnvs_));
    }

    static uint64_t composeSlotId(uint64_t generation, size_t envIndex, const char* kind) {
        if (generation == 0 || generation > std::numeric_limits<uint32_t>::max() ||
            envIndex >= static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::overflow_error(std::string("SelfPlayPool ") + kind + " id counter overflowed");
        }
        return (generation << 32) | static_cast<uint64_t>(envIndex + 1);
    }

    void beginEpisode(size_t envIndex) {
        ++episodeGenerations_[envIndex];
        episodeIds_[envIndex] = composeSlotId(episodeGenerations_[envIndex], envIndex, "episode");
    }

    void advanceState(size_t envIndex) {
        ++stateGenerations_[envIndex];
        stateIds_[envIndex] = composeSlotId(stateGenerations_[envIndex], envIndex, "state");
    }

    void resetOne(size_t envIndex) {
        envs_[envIndex].resetNative(seedFor(envIndex));
        ++resetCounts_[envIndex];
    }

    std::vector<GameEnv> makeDormantRoots() const {
        std::vector<GameEnv> roots;
        roots.reserve(envs_.size());
        for (const GameEnv& env : envs_) roots.emplace_back(env.cloneForMcts());
        return roots;
    }

    void requireActiveSearch() const {
        if (!searchActive_) {
            throw std::logic_error("submit_beliefs() is required before using the MCTS search API");
        }
    }

    void clearPacket(BeliefRequestBatch& out) const {
        const size_t rows = envs_.size();
        const size_t actionSlots = rows * static_cast<size_t>(maxActions_);
        std::fill_n(out.actionIds.mutable_data(), actionSlots, int32_t{-1});
        std::fill_n(out.actionFeatures.mutable_data(), actionSlots * kVectorActionFeatureCount, int32_t{0});
        std::fill_n(out.actionArgMasks.mutable_data(), actionSlots * kVectorActionArgMaskCount, uint8_t{0});
        std::fill_n(out.actionMask.mutable_data(), actionSlots, uint8_t{0});
        std::fill_n(out.reward.mutable_data(), rows, 0.0f);
        std::fill_n(out.terminated.mutable_data(), rows, uint8_t{0});
        std::fill_n(out.actionValid.mutable_data(), rows, uint8_t{0});
        std::fill_n(out.winner.mutable_data(), rows, int32_t{-1});
        std::fill_n(out.terminalValues.mutable_data(), rows * static_cast<size_t>(playerCount_), 0.0f);
    }

    void writeTerminalValues(float* out, int winner) const {
        std::fill_n(out, static_cast<size_t>(playerCount_), 0.0f);
        if (winner < 0 || winner >= playerCount_) return;
        const float loserValue = -1.0f / static_cast<float>(playerCount_ - 1);
        for (int player = 0; player < playerCount_; ++player) {
            out[player] = player == winner ? 1.0f : loserValue;
        }
    }

    void writeBeliefRequests(BeliefRequestBatch& out, const int32_t* actions, bool advance) {
        clearPacket(out);
        const size_t rows = envs_.size();
        const size_t tiles = tileCount();
        int32_t* mapData = out.mapTokens.mutable_data();
        int32_t* stateData = out.state.mutable_data();
        int32_t* actionIdData = out.actionIds.mutable_data();
        int32_t* actionFeatureData = out.actionFeatures.mutable_data();
        uint8_t* actionArgMaskData = out.actionArgMasks.mutable_data();
        uint8_t* actionMaskData = out.actionMask.mutable_data();
        int32_t* legalCountData = out.legalActionCount.mutable_data();
        int32_t* totalLegalCountData = out.totalLegalActionCount.mutable_data();
        uint8_t* actionTruncatedData = out.actionTruncated.mutable_data();
        float* rewardData = out.reward.mutable_data();
        uint8_t* terminatedData = out.terminated.mutable_data();
        uint8_t* actionValidData = out.actionValid.mutable_data();
        int32_t* winnerData = out.winner.mutable_data();
        float* terminalValuesData = out.terminalValues.mutable_data();
        int32_t* envIdData = out.envId.mutable_data();
        int32_t* toPlayData = out.toPlay.mutable_data();
        uint64_t* stateIdData = out.stateId.mutable_data();
        uint64_t* episodeIdData = out.episodeId.mutable_data();

        pool_.parallelFor(rows, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (advance) {
                    const int32_t actionId = actions[i];
                    const NativeStepResult result = actionId >= 0
                        ? envs_[i].stepNative(static_cast<size_t>(actionId))
                        : NativeStepResult{};
                    actionValidData[i] = result.ok ? 1 : 0;
                    rewardData[i] = result.reward;
                    terminatedData[i] = result.done ? 1 : 0;
                    winnerData[i] = result.winner;
                    if (result.done) {
                        writeTerminalValues(
                            terminalValuesData + i * static_cast<size_t>(playerCount_),
                            result.winner);
                    }
                    if (result.ok) {
                        if (result.done && autoReset_) {
                            resetOne(i);
                            beginEpisode(i);
                        }
                        advanceState(i);
                    }
                }

                envs_[i].writeTrainingPacket(
                    mapData + i * tiles * kMapTokenFeatureCount,
                    stateData + i * kVectorStateFeatureCount,
                    actionIdData + i * static_cast<size_t>(maxActions_),
                    actionFeatureData + i * static_cast<size_t>(maxActions_) * kVectorActionFeatureCount,
                    actionArgMaskData + i * static_cast<size_t>(maxActions_) * kVectorActionArgMaskCount,
                    actionMaskData + i * static_cast<size_t>(maxActions_),
                    legalCountData + i,
                    totalLegalCountData + i,
                    actionTruncatedData + i,
                    static_cast<size_t>(maxActions_),
                    false);
                envIdData[i] = static_cast<int32_t>(i);
                toPlayData[i] = static_cast<int32_t>(envs_[i].currentPlayerNative());
                stateIdData[i] = stateIds_[i];
                episodeIdData[i] = episodeIds_[i];
            }
        });

    }

    void attachLeafPositionMetadata(py::dict& out) const {
        auto treeIds = py::array_t<int32_t, py::array::c_style>::ensure(out["tree_id"]);
        if (!treeIds || treeIds.ndim() != 1) {
            throw std::logic_error("MCTS returned an invalid tree_id batch");
        }
        const size_t rows = static_cast<size_t>(treeIds.shape(0));
        py::array_t<int32_t> envIds(static_cast<py::ssize_t>(rows));
        py::array_t<uint64_t> stateIds(static_cast<py::ssize_t>(rows));
        py::array_t<uint64_t> episodeIds(static_cast<py::ssize_t>(rows));
        const int32_t* treeData = treeIds.data();
        int32_t* envData = envIds.mutable_data();
        uint64_t* stateData = stateIds.mutable_data();
        uint64_t* episodeData = episodeIds.mutable_data();
        for (size_t row = 0; row < rows; ++row) {
            const int32_t tree = treeData[row];
            if (tree < 0 || tree >= numEnvs_) throw std::logic_error("MCTS returned an out-of-range tree id");
            const size_t env = static_cast<size_t>(tree);
            envData[row] = tree;
            stateData[row] = stateIds_[env];
            episodeData[row] = episodeIds_[env];
        }
        out["env_id"] = std::move(envIds);
        out["state_id"] = std::move(stateIds);
        out["episode_id"] = std::move(episodeIds);
    }

    void attachRootPositionMetadata(py::dict& out) const {
        py::array_t<int32_t> envIds(static_cast<py::ssize_t>(envs_.size()));
        py::array_t<uint64_t> stateIds(static_cast<py::ssize_t>(envs_.size()));
        py::array_t<uint64_t> episodeIds(static_cast<py::ssize_t>(envs_.size()));
        int32_t* envData = envIds.mutable_data();
        uint64_t* stateData = stateIds.mutable_data();
        uint64_t* episodeData = episodeIds.mutable_data();
        for (size_t i = 0; i < envs_.size(); ++i) {
            envData[i] = static_cast<int32_t>(i);
            stateData[i] = stateIds_[i];
            episodeData[i] = episodeIds_[i];
        }
        out["env_id"] = std::move(envIds);
        out["state_id"] = std::move(stateIds);
        out["episode_id"] = std::move(episodeIds);
    }

    int numEnvs_ = 0;
    int mapSize_ = 0;
    std::vector<int> tribes_;
    int playerCount_ = 0;
    uint32_t baseSeed_ = 0;
    std::string unitsJsonPath_;
    MapType mapType_ = MapType::Lakes;
    int maxActions_ = 0;
    bool autoReset_ = true;
    float cPuct_ = 1.5f;
    std::vector<GameEnv> envs_;
    std::vector<uint64_t> resetCounts_;
    std::vector<uint64_t> stateGenerations_;
    std::vector<uint64_t> episodeGenerations_;
    std::vector<uint64_t> stateIds_;
    std::vector<uint64_t> episodeIds_;
    std::vector<int32_t> beliefValidationScratch_;
    BatchThreadPool pool_;
    std::unique_ptr<MctsPool> mcts_;
    bool searchActive_ = false;
    mutable std::mutex apiMutex_;
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

    py::enum_<MapType>(m, "MapType")
        .value("Lakes", MapType::Lakes)
        .value("Drylands", MapType::Drylands)
        .export_values();

    py::class_<GameEnv>(m, "GameEnv")
        .def(py::init<int, const std::vector<int>&, uint32_t, const std::string&, MapType>(),
             py::arg("map_size") = 16,
             py::arg("tribes") = std::vector<int>{3, 2},
             py::arg("seed") = 1u,
             py::arg("units_json_path") = "",
             py::arg("map_type") = MapType::Lakes)
        .def("reset", &GameEnv::reset,
             py::arg("map_size") = std::nullopt,
             py::arg("tribes") = std::nullopt,
             py::arg("seed") = std::nullopt,
             py::arg("units_json_path") = std::nullopt,
             py::arg("map_type") = std::nullopt)
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
             py::arg("visible_only") = true,
             py::arg("hidden_value") = -1)
        .def("tokenized_map", &GameEnv::tokenizedMap,
             py::arg("player_id") = std::nullopt,
             py::arg("visible_only") = true,
             py::arg("hidden_value") = -1)
        .def("player_map", &GameEnv::playerMap,
             py::arg("player_id") = std::nullopt,
             py::arg("hidden_value") = -1)
        .def("player_map_numpy", &GameEnv::playerMapNumpy,
             py::arg("player_id") = std::nullopt,
             py::arg("hidden_value") = -1)
        .def("full_map", &GameEnv::fullMap)
        .def("full_map_numpy", &GameEnv::fullMapNumpy)
        .def("lighthouse_discovered_by_masks", &GameEnv::lighthouseDiscoveredByMasks)
        .def("lighthouse_discovered_by_players", &GameEnv::lighthouseDiscoveredByPlayers)
        .def("lighthouse_visibility", &GameEnv::lighthouseVisibility,
             py::arg("player_id") = std::nullopt)
        .def("legal_action_mask", &GameEnv::legalActionMask)
        .def("legal_action_ids", &GameEnv::legalActionIds)
        .def("legal_action_ids_fast", &GameEnv::legalActionIdsFast)
        .def("action_param_spec", &GameEnv::actionParamSpec)
        .def("get_actions", &GameEnv::getActions)
        .def("legal_param_actions", &GameEnv::legalParamActions)
        .def("model_request", &GameEnv::modelRequest,
             "Returns the canonical model request packet: map_tokens, obs, actions, spec.")
        .def("request_packet", &GameEnv::requestPacket,
             "Alias for model_request().")
        .def("model_request_numpy", &GameEnv::modelRequestNumpy,
             "Returns the canonical model request packet with map/actions as NumPy arrays.")
        .def("request_packet_numpy", &GameEnv::requestPacketNumpy,
             "Alias for model_request_numpy().")
        .def("visible_events_numpy", &GameEnv::visibleEventsNumpy,
             py::arg("since") = 0u,
             "Returns only the current player's perspective-filtered event journal as NumPy arrays.")
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
        .def("world_seed", &GameEnv::worldSeed,
             "Returns the effective seed used to generate the current map.")
        .def("player_tribes", &GameEnv::playerTribes,
             "Returns tribe ids in player order for the current game.")
        .def("replay_action_ids", &GameEnv::replayActionIds,
             "Returns accepted action ids recorded for the current match.")
        .def("save_replay", &GameEnv::saveReplay, py::arg("path"),
             "Writes the current match as a .polygame replay.")
        .def("load_replay", &GameEnv::loadReplay, py::arg("path"),
             "Loads a .polygame replay and returns its final observation.")
        .def("get_techs", &GameEnv::getTechs,
             py::arg("player_id") = std::nullopt)
        .def("is_done", &GameEnv::isDone)
        .def("clone", &GameEnv::clone)
        .def("copy", &GameEnv::clone)
        .def("make_belief_env", &GameEnv::makeBeliefEnv,
             py::arg("completed_map_tokens"),
             py::arg("perspective") = std::nullopt,
             "Builds a detached hypothetical world from complete map tokens without copying hidden source state.")
        .def("save_state", &GameEnv::saveState)
        .def("load_state", &GameEnv::loadState, py::arg("snapshot"))
        .def("_last_revealed_indices", &GameEnv::lastRevealedIndices,
             py::arg("perspective") = std::nullopt,
             "Internal: tile indices revealed by the most recent step. "
             "Use GameEnv.last_revealed_tiles() instead.")
        .def("hidden_tile_indices", &GameEnv::hiddenTileIndices,
             py::arg("perspective") = std::nullopt,
             "Returns tile indices not yet observed by the given player (defaults to current player).")
        .def("apply_tile_predictions", &GameEnv::applyTilePredictions,
             py::arg("predictions"),
             py::arg("perspective") = std::nullopt,
             "Applies sparse predicted tile features to hidden tiles only.")
        ;

    py::class_<VectorGameEnv>(m, "VectorGameEnv")
        .def(py::init<int, int, const std::vector<int>&, uint32_t, const std::string&, MapType, int, int, bool, bool, int>(),
             py::arg("num_envs"),
             py::arg("map_size") = 11,
             py::arg("tribes") = std::vector<int>{3, 2},
             py::arg("seed") = 1u,
             py::arg("units_json_path") = "",
             py::arg("map_type") = MapType::Lakes,
             py::arg("num_threads") = 0,
             py::arg("max_actions") = 512,
             py::arg("auto_reset") = true,
             py::arg("include_combat_preview") = false,
             py::arg("visible_event_history") = 0,
             "Batched native training environment. All games are executed in C++.")
        .def("reset", &VectorGameEnv::reset,
             py::arg("seed") = std::nullopt,
             "Reset all environments and return one dense batch.")
        .def("step", &VectorGameEnv::step,
             py::arg("action_ids"),
             "Step all environments with one action-space id per environment.")
        .def("reset_into", &VectorGameEnv::resetInto,
             py::arg("buffers"), py::arg("seed") = std::nullopt,
             "Fill validated caller-owned batch arrays in place without allocating output arrays.")
        .def("step_into", &VectorGameEnv::stepInto,
             py::arg("action_ids"), py::arg("buffers"),
             "Step environments and fill validated caller-owned batch arrays in place.")
        .def("batch_spec", &VectorGameEnv::batchSpec,
             "Return exact dtype and shape requirements for reset_into()/step_into() buffers.")
        .def_property_readonly("num_envs", &VectorGameEnv::numEnvs)
        .def_property_readonly("num_threads", &VectorGameEnv::numThreads)
        .def_property_readonly("map_size", &VectorGameEnv::mapSize)
        .def_property_readonly("max_actions", &VectorGameEnv::maxActions)
        .def_property_readonly("auto_reset", &VectorGameEnv::autoReset)
        .def_property_readonly("include_combat_preview", &VectorGameEnv::includeCombatPreview)
        .def_property_readonly("visible_event_history", &VectorGameEnv::visibleEventHistory)
        .def_property_readonly("action_feature_names", &VectorGameEnv::actionFeatureNames)
        .def_property_readonly("state_feature_names", &VectorGameEnv::stateFeatureNames)
        .def_property_readonly("visible_event_feature_names", &VectorGameEnv::visibleEventFeatureNames)
        .def_property_readonly("visible_event_affected_feature_names", &VectorGameEnv::visibleEventAffectedFeatureNames)
        ;

    py::class_<MctsPool>(m, "MctsPool")
        .def(py::init<py::iterable, int, int, float>(),
             py::arg("roots"),
             py::arg("num_threads") = 0,
             py::arg("max_actions") = 512,
             py::arg("c_puct") = 1.5f,
             "Native multi-root PUCT scheduler with batched leaf evaluation. "
             "Two-player pools use scalar values; larger pools use one value per player.")
        .def("select_leaves", &MctsPool::selectLeaves,
             py::arg("max_leaves") = 0,
             "Return at most one pending leaf per tree as a dense model batch.")
        .def("select_leaves_into", &MctsPool::selectLeavesInto,
             py::arg("buffers"), py::arg("max_leaves") = 0,
             "Fill caller-owned leaf buffers and return the valid prefix length.")
        .def("expand_and_backup", &MctsPool::expandAndBackup,
             py::arg("leaf_ids"), py::arg("policy_logits"), py::arg("values"),
             "Expand pending leaves from batched policy logits and back up values.")
        .def("root_policy", &MctsPool::rootPolicy,
             py::arg("temperature") = 1.0f,
             "Return root visit counts and a masked action policy for every tree.")
        .def("root_policy_into", &MctsPool::rootPolicyInto,
             py::arg("buffers"), py::arg("temperature") = 1.0f,
             "Fill caller-owned root-policy buffers in place.")
        .def("leaf_batch_spec", &MctsPool::leafBatchSpec,
             py::arg("max_leaves") = 0,
             "Return buffer requirements for select_leaves_into().")
        .def("root_policy_spec", &MctsPool::rootPolicySpec,
             "Return buffer requirements for root_policy_into().")
        .def_property_readonly("num_trees", &MctsPool::numTrees)
        .def_property_readonly("num_threads", &MctsPool::numThreads)
        .def_property_readonly("max_actions", &MctsPool::maxActions)
        .def_property_readonly("map_size", &MctsPool::mapSize)
        .def_property_readonly("player_count", &MctsPool::playerCount)
        .def_property_readonly("num_players", &MctsPool::playerCount)
        .def_property_readonly("c_puct", &MctsPool::cPuct)
        .def_property_readonly("pending_count", &MctsPool::pendingCount)
        ;

    py::class_<SelfPlayPool>(m, "SelfPlayPool")
        .def(py::init<int, int, const std::vector<int>&, uint32_t, const std::string&, MapType, int, int, bool, float>(),
             py::arg("num_envs"),
             py::arg("map_size") = 11,
             py::arg("tribes") = std::vector<int>{3, 2},
             py::arg("seed") = 1u,
             py::arg("units_json_path") = "",
             py::arg("map_type") = MapType::Lakes,
             py::arg("num_threads") = 0,
             py::arg("max_actions") = 512,
             py::arg("auto_reset") = true,
             py::arg("c_puct") = 1.5f,
             "Native model-agnostic self-play scheduler with detached belief-root MCTS.")
        .def("reset", &SelfPlayPool::reset,
             py::arg("seed") = std::nullopt,
             "Reset live games and return dense player-view belief requests.")
        .def("reset_into", &SelfPlayPool::resetInto,
             py::arg("buffers"), py::arg("seed") = std::nullopt,
             "Reset live games and fill validated caller-owned belief buffers in place.")
        .def("belief_requests", &SelfPlayPool::beliefRequests,
             "Return current dense player-view inputs for an external belief model.")
        .def("belief_requests_into", &SelfPlayPool::beliefRequestsInto,
             py::arg("buffers"),
             "Fill validated caller-owned current belief buffers in place.")
        .def("submit_beliefs", &SelfPlayPool::submitBeliefs,
             py::arg("state_ids"), py::arg("completed_map_tokens"),
             "Validate detached belief worlds and make them the active MCTS roots.")
        .def("select_leaves", &SelfPlayPool::selectLeaves,
             py::arg("max_leaves") = 0,
             "Return a dense MCTS leaf batch for an external policy/value model.")
        .def("select_leaves_into", &SelfPlayPool::selectLeavesInto,
             py::arg("buffers"), py::arg("max_leaves") = 0,
             "Fill caller-owned MCTS leaf buffers and return the valid prefix length.")
        .def("expand_and_backup", &SelfPlayPool::expandAndBackup,
             py::arg("leaf_ids"), py::arg("policy_logits"), py::arg("values"),
             "Expand pending leaves and back up external policy/value results.")
        .def("root_policy", &SelfPlayPool::rootPolicy,
             py::arg("temperature") = 1.0f,
             "Return one root visit policy per live game.")
        .def("root_policy_into", &SelfPlayPool::rootPolicyInto,
             py::arg("buffers"), py::arg("temperature") = 1.0f,
             "Fill caller-owned root-policy buffers in place.")
        .def("step", &SelfPlayPool::step,
             py::arg("action_ids"),
             "Apply chosen action ids to live games and return their next belief requests.")
        .def("step_into", &SelfPlayPool::stepInto,
             py::arg("action_ids"), py::arg("buffers"),
             "Apply actions and fill validated caller-owned next-belief buffers in place.")
        .def("belief_batch_spec", &SelfPlayPool::beliefBatchSpec,
             "Return exact dtype and shape requirements for SelfPlayPool reusable belief buffers.")
        .def("leaf_batch_spec", &SelfPlayPool::leafBatchSpec,
             py::arg("max_leaves") = 0,
             "Return buffer requirements for SelfPlayPool.select_leaves_into().")
        .def("root_policy_spec", &SelfPlayPool::rootPolicySpec,
             "Return buffer requirements for SelfPlayPool.root_policy_into().")
        .def_property_readonly("num_envs", &SelfPlayPool::numEnvs)
        .def_property_readonly("num_threads", &SelfPlayPool::numThreads)
        .def_property_readonly("map_size", &SelfPlayPool::mapSize)
        .def_property_readonly("player_count", &SelfPlayPool::playerCount)
        .def_property_readonly("num_players", &SelfPlayPool::playerCount)
        .def_property_readonly("max_actions", &SelfPlayPool::maxActions)
        .def_property_readonly("auto_reset", &SelfPlayPool::autoReset)
        .def_property_readonly("c_puct", &SelfPlayPool::cPuct)
        .def_property_readonly("search_active", &SelfPlayPool::searchActive)
        .def_property_readonly("pending_count", &SelfPlayPool::pendingCount)
        .def_property_readonly("action_feature_names", &SelfPlayPool::actionFeatureNames)
        .def_property_readonly("state_feature_names", &SelfPlayPool::stateFeatureNames)
        ;
}
