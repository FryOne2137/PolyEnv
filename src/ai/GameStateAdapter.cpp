#include "GameStateAdapter.h"

#include <array>

#include "content/tech/TechDB.h"
#include "systems/TechSystem.h"
#include "systems/UnitSystem.h"

namespace {
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
    if (pid != game_.getCurrentPlayerId()) return out;

    // Modal state: city upgrade must be resolved before any other action.
    if (const Game::PendingCityUpgrade* pending = game_.peekPendingCityUpgrade(pid)) {
        Action a{};
        a.type = Action::Type::UpgradeCity;
        a.pid = pid;
        a.city = pending->cityId;
        a.upgrade = pending->opts.a;
        out.push_back(a);
        if (pending->opts.b != pending->opts.a) {
            a.upgrade = pending->opts.b;
            out.push_back(a);
        }
        return out;
    }

    Action endTurn{};
    endTurn.type = Action::Type::EndTurn;
    endTurn.pid = pid;
    out.push_back(endTurn);

    for (uint8_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        Action a{};
        a.type = Action::Type::BuyTech;
        a.pid = pid;
        a.tech = static_cast<TechId>(i);
        if (game_.canBuyTech(pid, a.tech)) out.push_back(a);
    }

    const Player& player = game_.getPlayer(pid);
    for (UnitId uid : player.getUnits()) {
        if (!UnitSystem::unitExists(game_, uid)) continue;

        for (Pos p : game_.reachable(pid, uid)) {
            Action a{};
            a.type = Action::Type::Move;
            a.pid = pid;
            a.unit = uid;
            a.target = p;
            out.push_back(a);
        }

        for (Pos p : game_.attackable(pid, uid)) {
            Action a{};
            a.type = Action::Type::Attack;
            a.pid = pid;
            a.unit = uid;
            a.target = p;
            out.push_back(a);
        }

        if (UnitSystem::hasSkill(game_, uid, UnitSkill::Heal)) {
            Action a{};
            a.type = Action::Type::Heal;
            a.pid = pid;
            a.unit = uid;
            out.push_back(a);
        }

        if (game_.canUpgradeRaftToScout(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::RaftToScout;
            out.push_back(a);
        }
        if (game_.canUpgradeRaftToRammer(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::RaftToRammer;
            out.push_back(a);
        }
        if (game_.canUpgradeRaftToBomber(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::RaftToBomber;
            out.push_back(a);
        }
        if (game_.canUnitBecomeVeteran(pid, uid)) {
            Action a{};
            a.type = Action::Type::UnitUpgrade;
            a.pid = pid;
            a.unit = uid;
            a.unitUpgrade = Action::UnitUpgradeKind::BecomeVeteran;
            out.push_back(a);
        }

        const Pos up = UnitSystem::getPos(game_, uid);
        Action ix{};
        ix.type = Action::Type::TileAction;
        ix.pid = pid;
        ix.unit = uid;
        ix.pos = up;

        ix.tileAction = Action::TileActionKind::Ruin;
        if (game_.canHandleRuin(pid, uid, up)) out.push_back(ix);

        ix.tileAction = Action::TileActionKind::Starfish;
        if (game_.canHandleStarfish(pid, uid, up)) out.push_back(ix);

        ix.tileAction = Action::TileActionKind::CaptureCity;
        if (game_.canHandleCityCapture(pid, uid, up)) out.push_back(ix);
    }

    const int w = game_.getMap().getWidth();
    const int h = game_.getMap().getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pos p{x, y};

            for (int bi = 1; bi <= 16; ++bi) {
                Action b{};
                b.type = Action::Type::Build;
                b.pid = pid;
                b.pos = p;
                b.building = static_cast<BuildingTypeEnum>(bi);
                if (game_.canBuildBuilding(pid, p, b.building)) out.push_back(b);
            }

            Action t{};
            t.type = Action::Type::TileAction;
            t.pid = pid;
            t.pos = p;

            t.tileAction = Action::TileActionKind::Hunt;
            if (game_.canHunt(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::Organization;
            if (game_.canOrganization(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::Fishing;
            if (game_.canFishing(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::ClearForest;
            if (game_.canClearForest(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::BurnForest;
            if (game_.canBurnForest(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::GrowForest;
            if (game_.canGrowForest(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::DestroyTile;
            if (game_.canDestroyTile(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::BuildRoad;
            if (game_.canBuildRoad(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::BuildBridge;
            if (game_.canBuildBridge(pid, p)) out.push_back(t);
            t.tileAction = Action::TileActionKind::FoundCity;
            if (game_.canFoundCityFromVillage(pid, p)) out.push_back(t);
        }
    }

    for (CityId cid : player.getCities()) {
        const City* city = game_.getCity(cid);
        if (!city) continue;
        const Pos spawnPos = city->getPos();

        for (UnitType ut : kTrainableUnitTypes) {
            if (!game_.canSpawnUnit(pid, ut, spawnPos, false)) continue;

            Action s{};
            s.type = Action::Type::SpawnUnit;
            s.pid = pid;
            s.pos = spawnPos;
            s.spawnType = ut;
            out.push_back(s);
        }
    }

    return out;
}

std::vector<uint8_t> GameStateAdapter::legalActionMask(PlayerId pid) const {
    if (pid != game_.getCurrentPlayerId()) {
        return std::vector<uint8_t>(actionSpace_.size(), 0);
    }
    return actionSpace_.legalMask(game_, legalActions(pid));
}

std::optional<Action> GameStateAdapter::decodeActionId(PlayerId pid, size_t actionId) const {
    if (pid != game_.getCurrentPlayerId()) return std::nullopt;
    if (actionId >= actionSpace_.size()) return std::nullopt;

    std::vector<uint8_t> mask = legalActionMask(pid);
    if (!mask[actionId]) return std::nullopt;

    return actionSpace_.decode(game_, actionId);
}

std::optional<size_t> GameStateAdapter::encodeActionId(const Action& action) const {
    return actionSpace_.encode(game_, action);
}

void GameStateAdapter::apply(const Action& a) {
    (void)applyAction(a);
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
