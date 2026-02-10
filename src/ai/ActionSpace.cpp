#include "ActionSpace.h"

#include "game/Game.h"
#include "world/Map.h"
#include "world/Tile.h"

ActionSpace::ActionSpace(int width, int height)
    : width_(width), height_(height), cellCount_(width * height) {
    if (width_ <= 0 || height_ <= 0) {
        width_ = 0;
        height_ = 0;
        cellCount_ = 0;
    }

    offEndTurn_ = 0;
    offBuyTech_ = offEndTurn_ + 1;
    offMove_ = offBuyTech_ + static_cast<size_t>(kTechCount);
    offAttack_ = offMove_ + static_cast<size_t>(cellCount_) * static_cast<size_t>(cellCount_);
    offHeal_ = offAttack_ + static_cast<size_t>(cellCount_) * static_cast<size_t>(cellCount_);
    offUnitUpgrade_ = offHeal_ + static_cast<size_t>(cellCount_);
    offTileAction_ = offUnitUpgrade_ + static_cast<size_t>(kUnitUpgradeCount) * static_cast<size_t>(cellCount_);
    offBuild_ = offTileAction_ + static_cast<size_t>(kTileActionCount) * static_cast<size_t>(cellCount_);
    offSpawn_ = offBuild_ + static_cast<size_t>(kBuildingCount) * static_cast<size_t>(cellCount_);
    offUpgradeCity_ = offSpawn_ + static_cast<size_t>(kSpawnUnitCount) * static_cast<size_t>(cellCount_);
    totalSize_ = offUpgradeCity_ + static_cast<size_t>(kCityUpgradeCount) * static_cast<size_t>(cellCount_);
}

std::optional<size_t> ActionSpace::encode(const Game& game, const Action& action) const {
    if (cellCount_ == 0) return std::nullopt;

    switch (action.type) {
        case Action::Type::EndTurn:
            return offEndTurn_;

        case Action::Type::BuyTech: {
            const int tech = static_cast<int>(action.tech);
            if (tech < 0 || tech >= kTechCount) return std::nullopt;
            return offBuyTech_ + static_cast<size_t>(tech);
        }

        case Action::Type::Move:
        case Action::Type::Attack: {
            const Unit* u = game.getUnit(action.unit);
            if (!u) return std::nullopt;
            const Pos from = u->getPos();
            if (!validPos(from) || !validPos(action.target)) return std::nullopt;
            const size_t packed = static_cast<size_t>(posToIndex(from)) * static_cast<size_t>(cellCount_)
                                + static_cast<size_t>(posToIndex(action.target));
            return (action.type == Action::Type::Move) ? (offMove_ + packed) : (offAttack_ + packed);
        }

        case Action::Type::Heal: {
            const Unit* u = game.getUnit(action.unit);
            if (!u) return std::nullopt;
            const Pos from = u->getPos();
            if (!validPos(from)) return std::nullopt;
            return offHeal_ + static_cast<size_t>(posToIndex(from));
        }

        case Action::Type::UnitUpgrade: {
            const int kind = idxForUnitUpgrade(action.unitUpgrade);
            if (kind < 0) return std::nullopt;
            const Unit* u = game.getUnit(action.unit);
            if (!u) return std::nullopt;
            const Pos from = u->getPos();
            if (!validPos(from)) return std::nullopt;
            return offUnitUpgrade_
                 + static_cast<size_t>(kind) * static_cast<size_t>(cellCount_)
                 + static_cast<size_t>(posToIndex(from));
        }

        case Action::Type::TileAction: {
            const int kind = idxForTileAction(action.tileAction);
            if (kind < 0 || !validPos(action.pos)) return std::nullopt;
            return offTileAction_
                 + static_cast<size_t>(kind) * static_cast<size_t>(cellCount_)
                 + static_cast<size_t>(posToIndex(action.pos));
        }

        case Action::Type::Build: {
            const int building = idxForBuilding(action.building);
            if (building < 0 || !validPos(action.pos)) return std::nullopt;
            return offBuild_
                 + static_cast<size_t>(building) * static_cast<size_t>(cellCount_)
                 + static_cast<size_t>(posToIndex(action.pos));
        }

        case Action::Type::SpawnUnit: {
            const int spawnType = idxForSpawnUnit(action.spawnType);
            if (spawnType < 0 || !validPos(action.pos)) return std::nullopt;
            return offSpawn_
                 + static_cast<size_t>(spawnType) * static_cast<size_t>(cellCount_)
                 + static_cast<size_t>(posToIndex(action.pos));
        }

        case Action::Type::UpgradeCity: {
            const int choice = idxForCityUpgrade(action.upgrade);
            if (choice < 0) return std::nullopt;
            const City* c = game.getCity(action.city);
            if (!c) return std::nullopt;
            const Pos p = c->getPos();
            if (!validPos(p)) return std::nullopt;
            return offUpgradeCity_
                 + static_cast<size_t>(choice) * static_cast<size_t>(cellCount_)
                 + static_cast<size_t>(posToIndex(p));
        }
    }

    return std::nullopt;
}

std::optional<Action> ActionSpace::decode(const Game& game, size_t actionId) const {
    if (actionId >= totalSize_ || cellCount_ == 0) return std::nullopt;

    Action a{};
    a.pid = game.getCurrentPlayerId();

    if (actionId == offEndTurn_) {
        a.type = Action::Type::EndTurn;
        return a;
    }

    if (actionId >= offBuyTech_ && actionId < offMove_) {
        a.type = Action::Type::BuyTech;
        a.tech = static_cast<TechId>(actionId - offBuyTech_);
        return a;
    }

    if (actionId >= offMove_ && actionId < offAttack_) {
        const size_t rel = actionId - offMove_;
        const int from = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int to = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        const Pos fromPos = indexToPos(from);
        a.type = Action::Type::Move;
        a.unit = game.getMap().unitOn(fromPos);
        a.target = indexToPos(to);
        return a;
    }

    if (actionId >= offAttack_ && actionId < offHeal_) {
        const size_t rel = actionId - offAttack_;
        const int from = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int to = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        const Pos fromPos = indexToPos(from);
        a.type = Action::Type::Attack;
        a.unit = game.getMap().unitOn(fromPos);
        a.target = indexToPos(to);
        return a;
    }

    if (actionId >= offHeal_ && actionId < offUnitUpgrade_) {
        const int from = static_cast<int>(actionId - offHeal_);
        a.type = Action::Type::Heal;
        a.unit = game.getMap().unitOn(indexToPos(from));
        return a;
    }

    if (actionId >= offUnitUpgrade_ && actionId < offTileAction_) {
        const size_t rel = actionId - offUnitUpgrade_;
        const int kind = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int from = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        a.type = Action::Type::UnitUpgrade;
        a.unitUpgrade = unitUpgradeFromIdx(kind);
        a.unit = game.getMap().unitOn(indexToPos(from));
        return a;
    }

    if (actionId >= offTileAction_ && actionId < offBuild_) {
        const size_t rel = actionId - offTileAction_;
        const int kind = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int pos = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        a.type = Action::Type::TileAction;
        a.tileAction = tileActionFromIdx(kind);
        a.pos = indexToPos(pos);
        return a;
    }

    if (actionId >= offBuild_ && actionId < offSpawn_) {
        const size_t rel = actionId - offBuild_;
        const int kind = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int pos = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        a.type = Action::Type::Build;
        a.building = buildingFromIdx(kind);
        a.pos = indexToPos(pos);
        return a;
    }

    if (actionId >= offSpawn_ && actionId < offUpgradeCity_) {
        const size_t rel = actionId - offSpawn_;
        const int kind = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int pos = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        a.type = Action::Type::SpawnUnit;
        a.spawnType = spawnUnitFromIdx(kind);
        a.pos = indexToPos(pos);
        return a;
    }

    if (actionId >= offUpgradeCity_ && actionId < totalSize_) {
        const size_t rel = actionId - offUpgradeCity_;
        const int kind = static_cast<int>(rel / static_cast<size_t>(cellCount_));
        const int pos = static_cast<int>(rel % static_cast<size_t>(cellCount_));
        const Pos p = indexToPos(pos);
        a.type = Action::Type::UpgradeCity;
        a.upgrade = cityUpgradeFromIdx(kind);

        if (game.getMap().inBounds(p)) {
            const Tile& t = game.getMap().at(p);
            if (t.getSettlementType() == SettlementTypeEnum::City) {
                a.city = static_cast<CityId>(t.getSettlementId());
            }
        }
        return a;
    }

    return std::nullopt;
}

std::vector<uint8_t> ActionSpace::legalMask(const Game& game, const std::vector<Action>& legalActions) const {
    std::vector<uint8_t> mask(totalSize_, 0);
    for (const Action& a : legalActions) {
        const std::optional<size_t> id = encode(game, a);
        if (id && *id < mask.size()) {
            mask[*id] = 1;
        }
    }
    return mask;
}

int ActionSpace::idxForTileAction(Action::TileActionKind kind) {
    const int v = static_cast<int>(kind);
    if (v <= 0 || v > kTileActionCount) return -1;
    return v - 1;
}

Action::TileActionKind ActionSpace::tileActionFromIdx(int idx) {
    if (idx < 0 || idx >= kTileActionCount) return Action::TileActionKind::None;
    return static_cast<Action::TileActionKind>(idx + 1);
}

int ActionSpace::idxForUnitUpgrade(Action::UnitUpgradeKind kind) {
    const int v = static_cast<int>(kind);
    if (v <= 0 || v > kUnitUpgradeCount) return -1;
    return v - 1;
}

Action::UnitUpgradeKind ActionSpace::unitUpgradeFromIdx(int idx) {
    if (idx < 0 || idx >= kUnitUpgradeCount) return Action::UnitUpgradeKind::None;
    return static_cast<Action::UnitUpgradeKind>(idx + 1);
}

int ActionSpace::idxForBuilding(BuildingTypeEnum b) {
    const int v = static_cast<int>(b);
    if (v <= 0 || v > kBuildingCount) return -1;
    return v - 1;
}

BuildingTypeEnum ActionSpace::buildingFromIdx(int idx) {
    if (idx < 0 || idx >= kBuildingCount) return BuildingTypeEnum::None;
    return static_cast<BuildingTypeEnum>(idx + 1);
}

int ActionSpace::idxForSpawnUnit(UnitType t) {
    const int v = static_cast<int>(t);
    if (v <= 0 || v > kSpawnUnitCount) return -1;
    return v - 1;
}

UnitType ActionSpace::spawnUnitFromIdx(int idx) {
    if (idx < 0 || idx >= kSpawnUnitCount) return UnitType::Unknown;
    return static_cast<UnitType>(idx + 1);
}

int ActionSpace::idxForCityUpgrade(CityUpgradeChoice c) {
    const int v = static_cast<int>(c);
    if (v <= 0 || v > kCityUpgradeCount) return -1;
    return v - 1;
}

CityUpgradeChoice ActionSpace::cityUpgradeFromIdx(int idx) {
    if (idx < 0 || idx >= kCityUpgradeCount) return CityUpgradeChoice::None;
    return static_cast<CityUpgradeChoice>(idx + 1);
}

int ActionSpace::posToIndex(Pos p) const {
    return p.y * width_ + p.x;
}

Pos ActionSpace::indexToPos(int idx) const {
    const int y = idx / width_;
    const int x = idx % width_;
    return Pos{x, y};
}

bool ActionSpace::validPos(Pos p) const {
    return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_;
}
