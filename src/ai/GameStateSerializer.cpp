#include "GameStateSerializer.h"

#include <algorithm>
#include <cstdint>

#include "content/buildings/BuildingDB.h"
#include "content/tech/TechDB.h"
#include "content/tribes/Tribe.h"
#include "content/units/Unit.h"
#include "content/units/UnitFactory.h"
#include "systems/BuildingSystem.h"
#include "systems/CitySystem.h"
#include "systems/DisbandSystem.h"
#include "systems/StarsSystem.h"
#include "systems/UnitSystem.h"
#include "world/Map.h"
#include "world/Tile.h"
#include "core/Ids.h"

using json = nlohmann::json;

// ── internal helpers ─────────────────────────────────────────────────────────

namespace {

static constexpr int kActionTypeCount  = static_cast<int>(Action::Type::UnitUpgrade) + 1;
static constexpr int kTechVocabSize    = static_cast<int>(TechId::Count) + 1;
static constexpr int kBuildingVocabSize = static_cast<int>(BuildingTypeEnum::Lighthouse) + 1;
static constexpr int kSpawnTypeVocabSize = static_cast<int>(UnitType::GiantSuper) + 1;
static constexpr int kCityUpgradeVocabSize = static_cast<int>(CityUpgradeChoice::SuperUnit) + 1;
static constexpr int kTileActionVocabSize = static_cast<int>(Action::TileActionKind::CaptureCity) + 1;
static constexpr int kUnitUpgradeVocabSize = static_cast<int>(Action::UnitUpgradeKind::Disband) + 1;
static constexpr int kPopulationGainVocabSize = 256;

struct ActionModelFields {
    int typeId        = -1;
    int sourceIndex   = -1;
    int targetIndex   = -1;
    int city          = -1;
    int tech          = static_cast<int>(TechId::Count);
    int building      = 0;
    int spawnType     = 0;
    int upgrade       = 0;
    int tileAction    = 0;
    int unitUpgrade   = 0;
    int unitId        = -1;
    int populationGain = 0;
    int costStars     = 0;
    int starsBefore   = 0;
    int affordable    = 1;
    int damageDealt   = -1;
    int damageReceived = -1;
};

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

static TechId requiredTechForTileAction(Action::TileActionKind kind) {
    switch (kind) {
        case Action::TileActionKind::Hunt:         return TechId::Hunting;
        case Action::TileActionKind::Organization: return TechId::Organization;
        case Action::TileActionKind::Fishing:      return TechId::Fishing;
        case Action::TileActionKind::ClearForest:  return TechId::Forestry;
        case Action::TileActionKind::BurnForest:   return TechId::Construction;
        case Action::TileActionKind::GrowForest:   return TechId::Spiritualism;
        case Action::TileActionKind::DestroyTile:  return TechId::Chivalry;
        case Action::TileActionKind::BuildRoad:    return TechId::Roads;
        case Action::TileActionKind::BuildBridge:  return TechId::Roads;
        case Action::TileActionKind::Starfish:     return TechId::Navigation;
        default:                                   return TechId::Count;
    }
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
        case Action::TileActionKind::Hunt:         return 2;
        case Action::TileActionKind::Organization: return 2;
        case Action::TileActionKind::Fishing:      return 2;
        case Action::TileActionKind::BurnForest:   return 5;
        case Action::TileActionKind::GrowForest:   return 5;
        case Action::TileActionKind::BuildRoad:    return 3;
        case Action::TileActionKind::BuildBridge:  return 5;
        default:                                   return 0;
    }
}

static TechId requiredTechForUnitUpgrade(Action::UnitUpgradeKind kind) {
    switch (kind) {
        case Action::UnitUpgradeKind::RaftToScout:  return TechId::Sailing;
        case Action::UnitUpgradeKind::RaftToRammer: return TechId::Ramming;
        case Action::UnitUpgradeKind::RaftToBomber: return TechId::Navigation;
        case Action::UnitUpgradeKind::Disband:      return TechId::FreeSpirit;
        default:                                     return TechId::Count;
    }
}

static int starsCostForUnitUpgrade(Action::UnitUpgradeKind kind) {
    switch (kind) {
        case Action::UnitUpgradeKind::RaftToScout:
            return std::max(0, UnitFactory::getUnitCost(UnitType::Scout));
        case Action::UnitUpgradeKind::RaftToRammer:
            return std::max(0, UnitFactory::getUnitCost(UnitType::Rammer));
        case Action::UnitUpgradeKind::RaftToBomber:
            return std::max(0, UnitFactory::getUnitCost(UnitType::Bomber));
        case Action::UnitUpgradeKind::Disband:
            return 0;
        default:
            return 0;
    }
}

static std::pair<int,int> predictAttackDamage(const Game& g, const Action& a) {
    if (a.type != Action::Type::Attack) return {-1, -1};
    if (a.unit == kNoUnit) return {-1, -1};
    if (!g.getMap().inBounds(a.target)) return {-1, -1};

    const Unit* attBefore = g.getUnit(a.unit);
    if (!attBefore) return {-1, -1};
    const int attHpBefore = std::max(0, attBefore->getHealth());

    const UnitId defId = g.getMap().unitOn(a.target);
    int defHpBefore = 0;
    if (defId != Map::kNoUnit) {
        if (const Unit* d = g.getUnit(defId)) defHpBefore = std::max(0, d->getHealth());
    }

    Game sim = g;
    if (!sim.attack(a.pid, a.unit, a.target)) return {-1, -1};

    int attHpAfter = 0;
    if (const Unit* u = sim.getUnit(a.unit)) attHpAfter = std::max(0, u->getHealth());

    int defHpAfter = 0;
    if (defId != Map::kNoUnit) {
        if (const Unit* d = sim.getUnit(defId)) defHpAfter = std::max(0, d->getHealth());
    }

    const int damageDealt    = (defId == Map::kNoUnit) ? 0 : std::max(0, defHpBefore - defHpAfter);
    const int damageReceived = std::max(0, attHpBefore - attHpAfter);
    return {damageDealt, damageReceived};
}

static ActionModelFields buildActionModelFields(const Game& g, const Action& a) {
    const Map& m = g.getMap();
    ActionModelFields f{};
    f.typeId = static_cast<int>(a.type);

    auto toIndex = [&m](Pos p) -> int {
        if (!m.inBounds(p)) return -1;
        return p.y * m.getWidth() + p.x;
    };
    auto unitPos = [&g](UnitId uid, Pos fb) -> Pos {
        if (uid == kNoUnit) return fb;
        if (const Unit* u = g.getUnit(uid)) return u->getPos();
        return fb;
    };

    Pos sourcePos = a.pos;
    Pos targetPos = a.target;
    bool hasSource = false;
    bool hasTarget = false;
    bool expectsUnit = false;

    switch (a.type) {
        case Action::Type::Move:
        case Action::Type::Attack:
            sourcePos   = unitPos(a.unit, a.pos);
            hasSource   = true;
            hasTarget   = true;
            expectsUnit = true;
            break;
        case Action::Type::Build:
            sourcePos = a.pos; targetPos = a.pos;
            hasSource = true; hasTarget = true;
            f.tech     = static_cast<int>(BuildingDB::getRequiredTech(a.building));
            f.building = static_cast<int>(a.building);
            f.populationGain = BuildingSystem::estimatePopulationGainForCity(g, a.pid, a.pos, a.building);
            f.costStars = BuildingDB::getCost(a.building);
            if (m.inBounds(a.pos)) {
                const Tile& t = m.at(a.pos);
                f.city = (t.getTerritoryCityId() == kNoCity)
                             ? -1
                             : static_cast<int>(t.getTerritoryCityId());
            }
            break;
        case Action::Type::SpawnUnit: {
            sourcePos = a.pos; targetPos = a.pos;
            hasSource = true; hasTarget = true;
            f.spawnType = static_cast<int>(a.spawnType);
            Unit probe  = UnitFactory::create(a.spawnType, a.pid, a.pos);
            f.tech      = static_cast<int>(probe.getRequiredTechToSpawn());
            f.costStars = std::max(0, probe.getCost());
            break;
        }
        case Action::Type::TileAction:
            sourcePos = a.pos; targetPos = a.pos;
            hasSource = true; hasTarget = true;
            expectsUnit  = tileActionUsesUnit(a.tileAction);
            f.tileAction = static_cast<int>(a.tileAction);
            f.tech       = static_cast<int>(requiredTechForTileAction(g, a));
            f.costStars  = starsCostForTileAction(a.tileAction);
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
                    sourcePos = c->getPos(); targetPos = c->getPos();
                    hasSource = true; hasTarget = true;
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
            hasSource   = true;
            expectsUnit = true;
            f.unitUpgrade = static_cast<int>(a.unitUpgrade);
            f.tech        = static_cast<int>(requiredTechForUnitUpgrade(a.unitUpgrade));
            f.costStars   = starsCostForUnitUpgrade(a.unitUpgrade);
            if (a.unitUpgrade == Action::UnitUpgradeKind::Disband) {
                f.costStars = -DisbandSystem::refundStars(g, a.unit);
            }
            break;
        case Action::Type::BuyTech:
            f.tech = static_cast<int>(a.tech);
            if (a.pid != kNoPlayer && static_cast<size_t>(a.pid) < g.getPlayers().size()) {
                const Player& p = g.getPlayer(a.pid);
                const int cityCount  = static_cast<int>(p.getCities().size());
                const bool hasLit    = p.hasTech(TechId::Philosophy);
                f.costStars = TechDB::calculatePrice(a.tech, cityCount, hasLit);
            }
            break;
        case Action::Type::EndTurn:
            break;
    }

    if (a.city != kNoCity && f.city < 0)
        f.city = static_cast<int>(a.city);

    if (expectsUnit) {
        if (a.unit != kNoUnit) {
            f.unitId = static_cast<int>(a.unit);
        } else if (hasSource && m.inBounds(sourcePos)) {
            const UnitId uid = m.unitOn(sourcePos);
            if (uid != Map::kNoUnit)
                f.unitId = static_cast<int>(uid);
        }
    }

    f.sourceIndex = hasSource ? toIndex(sourcePos) : -1;
    f.targetIndex = hasTarget ? toIndex(targetPos) : -1;

    if (a.pid != kNoPlayer && static_cast<size_t>(a.pid) < g.getPlayers().size())
        f.starsBefore = g.getPlayer(a.pid).getStars();

    f.affordable = (f.costStars <= f.starsBefore) ? 1 : 0;

    // Attack damage prediction
    const auto [dd, dr] = predictAttackDamage(g, a);
    f.damageDealt    = dd;
    f.damageReceived = dr;

    return f;
}

static std::string toSnakeCase(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv)
        out += (c == ' ') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static const char* actionTypeName(Action::Type t) {
    switch (t) {
        case Action::Type::Move:         return "move";
        case Action::Type::Attack:       return "attack";
        case Action::Type::Heal:         return "heal";
        case Action::Type::EndTurn:      return "end_turn";
        case Action::Type::BuyTech:      return "buy_tech";
        case Action::Type::UpgradeCity:  return "upgrade_city";
        case Action::Type::Build:        return "build";
        case Action::Type::SpawnUnit:    return "spawn_unit";
        case Action::Type::TileAction:   return "tile_action";
        case Action::Type::UnitUpgrade:  return "unit_upgrade";
    }
    return "unknown";
}

static const char* unitTypeName(UnitType t) {
    switch (t) {
        case UnitType::Warrior:    return "warrior";
        case UnitType::Archer:     return "archer";
        case UnitType::Defender:   return "defender";
        case UnitType::Rider:      return "rider";
        case UnitType::MindBender: return "mind_bender";
        case UnitType::Swordsman:  return "swordsman";
        case UnitType::Catapult:   return "catapult";
        case UnitType::Cloak:      return "cloak";
        case UnitType::Knight:     return "knight";
        case UnitType::Dagger:     return "dagger";
        case UnitType::Giant:      return "giant";
        case UnitType::Bunny:      return "bunny";
        case UnitType::Bunta:      return "bunta";
        case UnitType::Raft:       return "raft";
        case UnitType::Scout:      return "scout";
        case UnitType::Rammer:     return "rammer";
        case UnitType::Bomber:     return "bomber";
        case UnitType::Dinghy:     return "dinghy";
        case UnitType::Pirate:     return "pirate";
        case UnitType::Juggernaut: return "juggernaut";
        default:                   return "unknown";
    }
}

static const char* buildingName(BuildingTypeEnum b) {
    switch (b) {
        case BuildingTypeEnum::Farm:      return "farm";
        case BuildingTypeEnum::Forge:     return "forge";
        case BuildingTypeEnum::LumberHut: return "lumber_hut";
        case BuildingTypeEnum::Market:    return "market";
        case BuildingTypeEnum::Mine:      return "mine";
        case BuildingTypeEnum::Port:      return "port";
        case BuildingTypeEnum::Sawmill:   return "sawmill";
        case BuildingTypeEnum::Windmill:  return "windmill";
        case BuildingTypeEnum::Lighthouse:return "lighthouse";
        default:                          return "unknown";
    }
}

static const char* tileActionName(Action::TileActionKind k) {
    switch (k) {
        case Action::TileActionKind::Hunt:         return "hunt";
        case Action::TileActionKind::Organization: return "organization";
        case Action::TileActionKind::Fishing:      return "fishing";
        case Action::TileActionKind::ClearForest:  return "clear_forest";
        case Action::TileActionKind::BurnForest:   return "burn_forest";
        case Action::TileActionKind::GrowForest:   return "grow_forest";
        case Action::TileActionKind::DestroyTile:  return "destroy_tile";
        case Action::TileActionKind::BuildRoad:    return "build_road";
        case Action::TileActionKind::BuildBridge:  return "build_bridge";
        case Action::TileActionKind::Explorer:     return "explorer";
        case Action::TileActionKind::FoundCity:    return "found_city";
        case Action::TileActionKind::Ruin:         return "ruin";
        case Action::TileActionKind::Starfish:     return "starfish";
        case Action::TileActionKind::CaptureCity:  return "capture_city";
        default:                                   return "none";
    }
}

static const char* cityUpgradeName(CityUpgradeChoice c) {
    switch (c) {
        case CityUpgradeChoice::Workshop:         return "workshop";
        case CityUpgradeChoice::Explorer:         return "explorer";
        case CityUpgradeChoice::CityWall:         return "city_wall";
        case CityUpgradeChoice::Resources:        return "resources";
        case CityUpgradeChoice::PopulationGrowth: return "population_growth";
        case CityUpgradeChoice::BorderGrowth:     return "border_growth";
        case CityUpgradeChoice::Park:             return "park";
        case CityUpgradeChoice::SuperUnit:        return "super_unit";
        default:                                  return "none";
    }
}

static const char* unitUpgradeName(Action::UnitUpgradeKind k) {
    switch (k) {
        case Action::UnitUpgradeKind::RaftToScout:   return "raft_to_scout";
        case Action::UnitUpgradeKind::RaftToRammer:  return "raft_to_rammer";
        case Action::UnitUpgradeKind::RaftToBomber:  return "raft_to_bomber";
        case Action::UnitUpgradeKind::BecomeVeteran: return "become_veteran";
        case Action::UnitUpgradeKind::Disband:       return "disband";
        default:                                     return "none";
    }
}

static std::string actionFullName(const Action& a) {
    const char* base = actionTypeName(a.type);
    switch (a.type) {
        case Action::Type::BuyTech:
            if (a.tech < TechId::Count)
                return std::string(base) + ":" + toSnakeCase(TechDB::getTech(a.tech).name);
            break;
        case Action::Type::SpawnUnit:
            return std::string(base) + ":" + unitTypeName(a.spawnType);
        case Action::Type::Build:
            return std::string(base) + ":" + buildingName(a.building);
        case Action::Type::TileAction:
            return std::string(base) + ":" + tileActionName(a.tileAction);
        case Action::Type::UpgradeCity:
            return std::string(base) + ":" + cityUpgradeName(a.upgrade);
        case Action::Type::UnitUpgrade:
            return std::string(base) + ":" + unitUpgradeName(a.unitUpgrade);
        default:
            break;
    }
    return base;
}

// arg_mask: [source, target, tech, building, spawn_type, city_upgrade,
//            tile_action, unit_upgrade, unit_id, damage_dealt, damage_received, population_gain]
static std::vector<int> actionArgMask(const Action& a) {
    std::vector<int> mask(12, 0);
    switch (a.type) {
        case Action::Type::Move:
            mask[0]=1; mask[1]=1; mask[8]=1;
            break;
        case Action::Type::Attack:
            mask[0]=1; mask[1]=1; mask[8]=1; mask[9]=1; mask[10]=1;
            break;
        case Action::Type::Heal:
            mask[0]=1; mask[8]=1;
            break;
        case Action::Type::EndTurn:
            break;
        case Action::Type::BuyTech:
            mask[2]=1;
            break;
        case Action::Type::UpgradeCity:
            mask[0]=1; mask[5]=1;
            break;
        case Action::Type::Build:
            mask[0]=1; mask[1]=1; mask[2]=1; mask[3]=1; mask[11]=1;
            break;
        case Action::Type::SpawnUnit:
            mask[0]=1; mask[1]=1; mask[2]=1; mask[4]=1;
            break;
        case Action::Type::TileAction:
            mask[0]=1; mask[1]=1; mask[2]=1; mask[6]=1;
            if (tileActionUsesUnit(a.tileAction)) mask[8]=1;
            break;
        case Action::Type::UnitUpgrade:
            mask[0]=1; mask[2]=1; mask[7]=1; mask[8]=1;
            break;
    }
    return mask;
}

} // anonymous namespace

// ── public API ───────────────────────────────────────────────────────────────

namespace GameStateSerializer {

json tokenizedMapAsJson(const Game& game, PlayerId perspective) {
    const Map& m = game.getMap();
    const int w = m.getWidth();
    const int h = m.getHeight();

    bool hasClimbing     = false;
    bool hasOrganization = false;
    bool hasSailing      = false;
    if (perspective != kNoPlayer && static_cast<size_t>(perspective) < game.getPlayers().size()) {
        const Player& p = game.getPlayer(perspective);
        hasClimbing     = p.hasTech(TechId::Climbing);
        hasOrganization = p.hasTech(TechId::Organization);
        hasSailing      = p.hasTech(TechId::Sailing);
    }

    // Cloak (hide) detection helpers
    auto hasActivatedHide = [](const Unit* u) -> bool {
        if (!u || !u->hasSkill(UnitSkill::Hide)) return false;
        const Pos d = u->getLastMoveDir();
        return !(d.x == 0 && d.y == 0);
    };
    auto isEnemyHidden = [&](const Unit* u) -> bool {
        if (!u || perspective == kNoPlayer) return false;
        return (static_cast<int>(u->getOwnerId()) != static_cast<int>(perspective))
               && hasActivatedHide(u);
    };
    auto hasAdjacentEnemyHidden = [&](Pos center) -> bool {
        if (perspective == kNoPlayer) return false;
        static constexpr int dx[8] = {1,-1, 0, 0, 1, 1,-1,-1};
        static constexpr int dy[8] = {0, 0, 1,-1, 1,-1, 1,-1};
        for (int i = 0; i < 8; ++i) {
            const Pos np{center.x + dx[i], center.y + dy[i]};
            if (!m.inBounds(np)) continue;
            const UnitId nuid = m.unitOn(np);
            if (nuid == Map::kNoUnit) continue;
            if (isEnemyHidden(game.getUnit(nuid))) return true;
        }
        return false;
    };

    json tiles = json::array();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pos p{x, y};
            const Tile& t = m.at(p);

            const uint16_t visMask = static_cast<uint16_t>(t.getVisibility());
            const int visibility = (perspective != kNoPlayer && perspective < 16 &&
                                    ((visMask & (uint16_t(1) << perspective)) != 0))
                                   ? 1 : 0;

            int unitHp           = -1;
            int unitOwner        = -1;
            int unitId_v         = -1;
            int isCloakAround    = 0;
            int capitalLayer     = -1;
            int cityLevel        = -1;
            int ownUnitKills     = -1;
            int resourceToken    = static_cast<int>(t.getResource());
            int settlementType   = static_cast<int>(t.getSettlementType());
            int settlementId_v   = (static_cast<int>(t.getSettlementId()) == static_cast<int>(kNoSettlement))
                                   ? -1 : static_cast<int>(t.getSettlementId());
            int cityOwner        = -1;
            int cityUnitsOccupied = -1;

            if (!hasClimbing && resourceToken == static_cast<int>(ResourcesEnum::Metal))
                resourceToken = static_cast<int>(ResourcesEnum::None);
            if (!hasOrganization && resourceToken == static_cast<int>(ResourcesEnum::Crops))
                resourceToken = static_cast<int>(ResourcesEnum::None);
            if (!hasSailing && settlementType == static_cast<int>(SettlementTypeEnum::Starfish)) {
                settlementType = static_cast<int>(SettlementTypeEnum::None);
                settlementId_v = -1;
            }

            if (t.getSettlementType() == SettlementTypeEnum::City) {
                capitalLayer = 0;
                const CityId cid = static_cast<CityId>(t.getSettlementId());
                if (cid != kNoCity) {
                    if (const City* city = game.getCity(cid)) {
                        cityLevel = static_cast<int>(city->getLevel());
                        const PlayerId owner = city->getOwnerId();
                        if (owner != kNoPlayer) {
                            cityOwner = static_cast<int>(owner);
                        }
                        if (owner != kNoPlayer &&
                            static_cast<int>(owner) == static_cast<int>(perspective)) {
                            cityUnitsOccupied = static_cast<int>(CitySystem::getCityUnitsCount(game, cid));
                        }
                        if (city->isCapitalCity()) {
                            capitalLayer = 1;
                        }
                    }
                }
            }

            const UnitId uid = m.unitOn(p);
            if (uid != Map::kNoUnit) {
                if (const Unit* u = game.getUnit(uid)) {
                    if (!isEnemyHidden(u)) {
                        unitId_v  = static_cast<int>(uid);
                        unitHp    = u->getHealth();
                        unitOwner = static_cast<int>(u->getOwnerId());
                        if (perspective != kNoPlayer &&
                            static_cast<int>(u->getOwnerId()) == static_cast<int>(perspective)) {
                            ownUnitKills = u->getKillCounter();
                        }
                    }
                    if (perspective != kNoPlayer &&
                        static_cast<int>(u->getOwnerId()) == static_cast<int>(perspective) &&
                        hasAdjacentEnemyHidden(p)) {
                        isCloakAround = 1;
                    }
                }
            }

            const int territoryCityId = (static_cast<int>(t.getTerritoryCityId()) == static_cast<int>(kNoCity))
                                        ? -1 : static_cast<int>(t.getTerritoryCityId());

            // 18 features per tile (as floats)
            tiles.push_back(json::array({
                (float)visibility,
                (float)isCloakAround,
                (float)unitHp,
                (float)unitOwner,
                (float)unitId_v,
                (float)ownUnitKills,
                (float)territoryCityId,
                (float)static_cast<int>(t.getRoadBridge()),
                (float)static_cast<int>(t.getBuildingType()),
                (float)capitalLayer,
                (float)cityLevel,
                (float)settlementType,
                (float)settlementId_v,
                (float)cityOwner,
                (float)cityUnitsOccupied,
                (float)resourceToken,
                (float)static_cast<int>(t.getBaseTerrain()),
                (float)static_cast<int>(t.getTribe()),
            }));
        }
    }
    return tiles;
}

json observationAsJson(const Game& game, PlayerId perspective,
                       const json& tokenizedMap) {
    json obs;
    obs["turn"]           = game.getTurnNumber();
    obs["current_player"] = static_cast<int>(game.getCurrentPlayerId());
    obs["game_over"]      = game.isGameOver();
    obs["winner"]         = game.isGameOver() ? static_cast<int>(game.getWinner()) : -1;
    obs["map_size"]       = game.getMap().getWidth();

    int perspStars = -1;
    int ownUnits   = 0;
    int ownCities  = 0;
    int nextIncome = 0;

    if (perspective != kNoPlayer && static_cast<size_t>(perspective) < game.getPlayers().size()) {
        const Player& p = game.getPlayer(perspective);
        obs["tribe"] = Tribe::defaultName(p.getTribeType());
        perspStars = p.getStars();
        ownUnits   = static_cast<int>(p.getUnits().size());
        ownCities  = static_cast<int>(p.getCities().size());

        for (CityId cid : p.getCities()) {
            if (!CitySystem::cityExists(game, cid)) continue;
            if (CitySystem::isCityUnderSiege(game, cid)) continue;
            if (CitySystem::getCityIsInfiltrated(game, cid)) continue;
            nextIncome += static_cast<int>(CitySystem::getCityStarsPerRound(game, cid));
            nextIncome += StarsSystem::marketIncomeForCity(game, perspective, cid);
        }
    }

    obs["player_stars"]         = perspStars;
    obs["owns_units"]           = ownUnits;
    obs["own_cities"]           = ownCities;
    obs["next_turn_star_income"] = std::max(0, nextIncome);

    const Game::PendingCityUpgrade* pending = game.peekPendingCityUpgrade(game.getCurrentPlayerId());
    obs["pending_city_upgrade"] = (pending != nullptr);
    obs["pending_city_id"]      = pending ? static_cast<int>(pending->cityId) : -1;
    obs["pending_upgrade_a"]    = pending ? static_cast<int>(pending->opts.a) : static_cast<int>(CityUpgradeChoice::None);
    obs["pending_upgrade_b"]    = pending ? static_cast<int>(pending->opts.b) : static_cast<int>(CityUpgradeChoice::None);

    return obs;
}

json legalActionsAsJson(const Game& game, GameStateAdapter& adapter) {
    const PlayerId pid    = game.getCurrentPlayerId();
    const int currentStars = game.getPlayer(pid).getStars();

    json actions = json::array();
    for (size_t aid : adapter.legalActionIds(pid)) {
        const auto a = adapter.decodeActionId(pid, aid);
        if (!a) continue;

        const ActionModelFields f = buildActionModelFields(game, *a);

        json d;
        d["action_id"]      = (int)aid;
        d["type"]           = actionTypeName(a->type);
        d["type_fullname"]  = actionFullName(*a);
        d["type_id"]        = f.typeId;
        d["source_index"]   = f.sourceIndex;
        d["target_index"]   = f.targetIndex;
        d["city"]           = f.city;
        d["tech"]           = f.tech;
        d["building"]       = f.building;
        d["spawn_type"]     = f.spawnType;
        d["upgrade"]        = f.upgrade;
        d["tile_action"]    = f.tileAction;
        d["unit_upgrade"]   = f.unitUpgrade;
        d["unit_id"]        = f.unitId;
        d["unit"]           = f.unitId;
        d["cost_stars"]     = f.costStars;
        d["stars_before"]   = currentStars;
        d["affordable"]     = (f.costStars <= currentStars) ? 1 : 0;
        d["damage_dealt"]   = f.damageDealt;
        d["damage_received"] = f.damageReceived;
        d["arg_mask"]       = actionArgMask(*a);
        actions.push_back(std::move(d));
    }
    return actions;
}

json actionSpecAsJson(const Game& game) {
    const Map& m = game.getMap();
    json spec;
    spec["tile_vocab_size"]        = m.getWidth() * m.getHeight();
    spec["type_vocab_size"]        = kActionTypeCount;
    spec["tech_vocab_size"]        = kTechVocabSize;
    spec["building_vocab_size"]    = kBuildingVocabSize;
    spec["spawn_type_vocab_size"]  = kSpawnTypeVocabSize;
    spec["city_upgrade_vocab_size"] = kCityUpgradeVocabSize;
    spec["tile_action_vocab_size"] = kTileActionVocabSize;
    spec["unit_upgrade_vocab_size"] = kUnitUpgradeVocabSize;
    spec["population_gain_vocab_size"] = kPopulationGainVocabSize;
    spec["map_width"]              = m.getWidth();
    spec["map_height"]             = m.getHeight();
    spec["arg_order"] = json::array({
        "source_index", "target_index", "tech", "building",
        "spawn_type", "upgrade", "tile_action", "unit_upgrade",
        "unit_id", "damage_dealt", "damage_received", "population_gain"
    });
    return spec;
}

json buildRequest(const Game& game, GameStateAdapter& adapter) {
    const PlayerId pid = game.getCurrentPlayerId();

    json mapTokens = tokenizedMapAsJson(game, pid);
    json obs       = observationAsJson(game, pid, mapTokens);
    json actions   = legalActionsAsJson(game, adapter);
    json spec      = actionSpecAsJson(game);

    json req;
    req["map_tokens"] = std::move(mapTokens);
    req["obs"]        = std::move(obs);
    req["actions"]    = std::move(actions);
    req["spec"]       = std::move(spec);
    return req;
}

} // namespace GameStateSerializer
