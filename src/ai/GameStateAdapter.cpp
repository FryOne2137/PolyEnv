#include "GameStateAdapter.h"

#include <array>

#include "content/tech/TechDB.h"
#include "systems/TechSystem.h"
#include "systems/UnitSystem.h"

namespace {
static bool isTileVisibleToPlayer(const Game& game, PlayerId pid, Pos p) {
    return game.isTileVisibleForPlayer(pid, p);
}

static constexpr std::array<UnitType, 9> kTrainableUnitTypes = {
    UnitType::Warrior,
    UnitType::Archer,
    UnitType::Defender,
    UnitType::Rider,
    UnitType::MindBender,
    UnitType::Swordsman,
    UnitType::Catapult,
    UnitType::Cloak,
    UnitType::Knight,
};

template <typename EmitFn>
void enumerateLegalActions(const Game& game, PlayerId pid, EmitFn&& emit) {
    if (pid != game.getCurrentPlayerId()) return;

    // Modal state: city upgrade must be resolved before any other action.
    if (const Game::PendingCityUpgrade* pending = game.peekPendingCityUpgrade(pid)) {
        Action a{};
        a.type = Action::Type::UpgradeCity;
        a.pid = pid;
        a.city = pending->cityId;
        a.upgrade = pending->opts.a;
        emit(a);
        if (pending->opts.b != pending->opts.a) {
            a.upgrade = pending->opts.b;
            emit(a);
        }
        return;
    }

    Action endTurn{};
    endTurn.type = Action::Type::EndTurn;
    endTurn.pid = pid;
    emit(endTurn);

    for (uint8_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        Action a{};
        a.type = Action::Type::BuyTech;
        a.pid = pid;
        a.tech = static_cast<TechId>(i);
        if (game.canBuyTech(pid, a.tech)) emit(a);
    }

    const Player& player = game.getPlayer(pid);
    for (UnitId uid : player.getUnits()) {
        if (!UnitSystem::unitExists(game, uid)) continue;

        for (Pos p : game.reachable(pid, uid)) {
            if (!isTileVisibleToPlayer(game, pid, p)) continue;
            Action a{};
            a.type = Action::Type::Move;
            a.pid = pid;
            a.unit = uid;
            a.target = p;
            emit(a);
        }

        for (Pos p : game.attackable(pid, uid)) {
            if (!isTileVisibleToPlayer(game, pid, p)) continue;
            Action a{};
            a.type = Action::Type::Attack;
            a.pid = pid;
            a.unit = uid;
            a.target = p;
            emit(a);
        }

        if (UnitSystem::hasSkill(game, uid, UnitSkill::Heal)) {
            Action a{};
            a.type = Action::Type::Heal;
            a.pid = pid;
            a.unit = uid;
            emit(a);
        }

        if (game.canUpgradeRaftToScout(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::RaftToScout;
            emit(a);
        }
        if (game.canUpgradeRaftToRammer(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::RaftToRammer;
            emit(a);
        }
        if (game.canUpgradeRaftToBomber(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::RaftToBomber;
            emit(a);
        }
        if (game.canUnitBecomeVeteran(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::BecomeVeteran;
            emit(a);
        }

        const Pos up = UnitSystem::getPos(game, uid);
        if (isTileVisibleToPlayer(game, pid, up)) {
            Action ix{};
            ix.type = Action::Type::TileAction;
            ix.pid = pid;
            ix.unit = uid;
            ix.pos = up;

            ix.tileAction = Action::TileActionKind::Ruin;
            if (game.canHandleRuin(pid, uid, up)) emit(ix);

            ix.tileAction = Action::TileActionKind::Starfish;
            if (game.canHandleStarfish(pid, uid, up)) emit(ix);

            ix.tileAction = Action::TileActionKind::CaptureCity;
            if (game.canHandleCityCapture(pid, uid, up)) emit(ix);
        }
    }

    std::vector<Pos> ownedVisibleTiles;
    std::vector<Pos> neutralVisibleTiles;
    const std::vector<Pos>& allVisibleTiles = game.getVisibleTiles(pid);
    ownedVisibleTiles.reserve(allVisibleTiles.size());
    neutralVisibleTiles.reserve(allVisibleTiles.size() / 2 + 1);

    for (const Pos p : allVisibleTiles) {
        const Tile& tile = game.getMap().at(p);
        const CityId cid = tile.getTerritoryCityId();
        if (cid == kNoCity) {
            neutralVisibleTiles.push_back(p);
            continue;
        }
        const City* city = game.getCity(cid);
        if (city && static_cast<PlayerId>(city->getOwnerId()) == pid) {
            ownedVisibleTiles.push_back(p);
        }
    }

    for (const Pos p : ownedVisibleTiles) {
        for (int bi = 1; bi <= 16; ++bi) {
            Action b{};
            b.type = Action::Type::Build;
            b.pid = pid;
            b.pos = p;
            b.building = static_cast<BuildingTypeEnum>(bi);
            if (game.canBuildBuilding(pid, p, b.building)) emit(b);
        }
    }

    for (const Pos p : ownedVisibleTiles) {
        Action t{};
        t.type = Action::Type::TileAction;
        t.pid = pid;
        t.pos = p;

        t.tileAction = Action::TileActionKind::Hunt;
        if (game.canHunt(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::Organization;
        if (game.canOrganization(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::Fishing;
        if (game.canFishing(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::ClearForest;
        if (game.canClearForest(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::BurnForest;
        if (game.canBurnForest(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::GrowForest;
        if (game.canGrowForest(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::DestroyTile;
        if (game.canDestroyTile(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::BuildRoad;
        if (game.canBuildRoad(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::BuildBridge;
        if (game.canBuildBridge(pid, p)) emit(t);
    }

    // Road/bridge can also be built on neutral territory.
    for (const Pos p : neutralVisibleTiles) {
        Action t{};
        t.type = Action::Type::TileAction;
        t.pid = pid;
        t.pos = p;

        t.tileAction = Action::TileActionKind::BuildRoad;
        if (game.canBuildRoad(pid, p)) emit(t);
        t.tileAction = Action::TileActionKind::BuildBridge;
        if (game.canBuildBridge(pid, p)) emit(t);
    }

    // Keep village founding available on visible tiles.
    for (const Pos p : allVisibleTiles) {
        Action t{};
        t.type = Action::Type::TileAction;
        t.pid = pid;
        t.pos = p;
        t.tileAction = Action::TileActionKind::FoundCity;
        if (game.canFoundCityFromVillage(pid, p)) emit(t);
    }

    for (CityId cid : player.getCities()) {
        const City* city = game.getCity(cid);
        if (!city) continue;
        const Pos spawnPos = city->getPos();
        if (!isTileVisibleToPlayer(game, pid, spawnPos)) continue;

        for (UnitType ut : kTrainableUnitTypes) {
            if (!game.canSpawnUnit(pid, ut, spawnPos, false)) continue;

            Action s{};
            s.type = Action::Type::SpawnUnit;
            s.pid = pid;
            s.pos = spawnPos;
            s.spawnType = ut;
            emit(s);
        }
    }
}
}

GameStateAdapter::GameStateAdapter(Game game)
    : game_(std::move(game))
    , actionSpace_(game_.getMap().getWidth(), game_.getMap().getHeight()) {}

PlayerId GameStateAdapter::currentPlayer() const {
    return game_.getCurrentPlayerId();
}

bool GameStateAdapter::isTerminal() const {
    if (game_.isGameOver()) return true;

    int alive = 0;
    for (const Player& p : game_.getPlayers()) {
        if (!p.getCities().empty() || !p.getUnits().empty()) {
            ++alive;
            if (alive > 1) return false;
        }
    }
    return true;
}

std::vector<Action> GameStateAdapter::legalActions(PlayerId pid) const {
    std::vector<Action> out;
    enumerateLegalActions(game_, pid, [&](const Action& a) { out.push_back(a); });

    return out;
}

std::vector<uint8_t> GameStateAdapter::legalActionMask(PlayerId pid) const {
    if (pid != game_.getCurrentPlayerId()) {
        return std::vector<uint8_t>(actionSpace_.size(), 0);
    }
    ensureLegalCache(pid);
    return legalMaskCache_;
}

std::optional<Action> GameStateAdapter::decodeActionId(PlayerId pid, size_t actionId) const {
    if (pid != game_.getCurrentPlayerId()) return std::nullopt;
    ensureLegalCache(pid);
    if (actionId >= legalMaskCache_.size()) return std::nullopt;
    if (!legalMaskCache_[actionId]) return std::nullopt;

    return actionSpace_.decode(game_, actionId);
}

std::optional<size_t> GameStateAdapter::encodeActionId(const Action& action) const {
    return actionSpace_.encode(game_, action);
}

void GameStateAdapter::apply(const Action& a) {
    if (applyAction(a)) {
        invalidateLegalCache();
    }
}

std::vector<size_t> GameStateAdapter::legalActionIds(PlayerId pid) const {
    if (pid != game_.getCurrentPlayerId()) return {};
    ensureLegalCache(pid);
    return legalIdsCache_;
}

void GameStateAdapter::invalidateLegalCache() {
    legalCacheValid_ = false;
    legalCachePid_ = kNoPlayer;
    legalIdsCache_.clear();
    legalMaskCache_.clear();
}

void GameStateAdapter::ensureLegalCache(PlayerId pid) const {
    if (legalCacheValid_ && legalCachePid_ == pid) return;

    legalMaskCache_.assign(actionSpace_.size(), 0);
    legalIdsCache_.clear();

    enumerateLegalActions(game_, pid, [&](const Action& action) {
        const std::optional<size_t> id = actionSpace_.encode(game_, action);
        if (!id.has_value()) return;
        const size_t actionId = *id;
        if (actionId >= legalMaskCache_.size()) return;
        if (legalMaskCache_[actionId]) return; // avoid duplicate ids
        legalMaskCache_[actionId] = 1;
        legalIdsCache_.push_back(actionId);
    });

    legalCachePid_ = pid;
    legalCacheValid_ = true;
}

bool GameStateAdapter::applyAction(const Action& a) {
    switch (a.type) {
        case Action::Type::Move:
            return game_.moveUnit(a.pid, a.unit, a.target);
        case Action::Type::Attack:
            return game_.attack(a.pid, a.unit, a.target);
        case Action::Type::Heal:
            return game_.heal(a.pid, a.unit);
        case Action::Type::EndTurn:
            return game_.endTurn(a.pid);
        case Action::Type::BuyTech:
            return game_.buyTech(a.pid, a.tech);
        case Action::Type::UpgradeCity:
            return game_.upgradeCity(a.pid, a.city, a.upgrade);
        case Action::Type::Build:
            return game_.buildBuilding(a.pid, a.pos, a.building);
        case Action::Type::SpawnUnit:
            return game_.spawnUnit(a.spawnType, a.pid, a.pos, false) != kNoUnit;
        case Action::Type::TileAction:
            switch (a.tileAction) {
                case Action::TileActionKind::Hunt: return game_.hunt(a.pid, a.pos);
                case Action::TileActionKind::Organization: return game_.organization(a.pid, a.pos);
                case Action::TileActionKind::Fishing: return game_.fishing(a.pid, a.pos);
                case Action::TileActionKind::ClearForest: return game_.clearForest(a.pid, a.pos);
                case Action::TileActionKind::BurnForest: return game_.burnForest(a.pid, a.pos);
                case Action::TileActionKind::GrowForest: return game_.growForest(a.pid, a.pos);
                case Action::TileActionKind::DestroyTile: return game_.destroyTile(a.pid, a.pos);
                case Action::TileActionKind::BuildRoad: return game_.buildRoad(a.pid, a.pos);
                case Action::TileActionKind::BuildBridge: return game_.buildBridge(a.pid, a.pos);
                case Action::TileActionKind::Explorer: return game_.explorer(a.pid, a.pos);
                case Action::TileActionKind::FoundCity: return game_.foundCityFromVillage(a.pid, a.pos);
                case Action::TileActionKind::Ruin: return game_.handleRuin(a.pid, a.unit, a.pos);
                case Action::TileActionKind::Starfish: return game_.handleStarfish(a.pid, a.unit, a.pos);
                case Action::TileActionKind::CaptureCity: return game_.handleCityCapture(a.pid, a.unit, a.pos);
                case Action::TileActionKind::None: return false;
            }
            return false;
        case Action::Type::UnitUpgrade:
            switch (a.unitUpgrade) {
                case Action::UnitUpgradeKind::RaftToScout: return game_.upgradeRaftToScout(a.pid, a.unit);
                case Action::UnitUpgradeKind::RaftToRammer: return game_.upgradeRaftToRammer(a.pid, a.unit);
                case Action::UnitUpgradeKind::RaftToBomber: return game_.upgradeRaftToBomber(a.pid, a.unit);
                case Action::UnitUpgradeKind::BecomeVeteran: return game_.becomeVeteran(a.pid, a.unit);
                case Action::UnitUpgradeKind::None: return false;
            }
            return false;
    }
    return false;
}
