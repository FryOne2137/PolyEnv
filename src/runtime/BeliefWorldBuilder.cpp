#include "runtime/BeliefWorldBuilder.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "content/units/UnitFactory.h"
#include "game/Game.h"

namespace {
constexpr size_t kBeliefTokenWidth = 23;
constexpr int kUnitTypeMax = static_cast<int>(UnitType::GiantSuper);

bool inRange(int value, int minimum, int maximum) {
    return value >= minimum && value <= maximum;
}

void validateTokens(const Game& source, PlayerId perspective,
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
    for (const auto& t : tokens) {
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
    validateTokens(observedSource, perspective, tokens);
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

    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto& v = tokens[static_cast<size_t>(y * width + x)];
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
    for (const auto& v : tokens) if (v[14] == static_cast<int>(SettlementTypeEnum::City)) maxCityId = std::max(maxCityId, v[15]);
    belief.cities.resize(static_cast<size_t>(maxCityId + 1));
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto& v = tokens[static_cast<size_t>(y * width + x)];
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
        const auto& v = tokens[static_cast<size_t>(y * width + x)];
        if (v[4] == -1) continue;
        const UnitId unitId = static_cast<UnitId>(belief.units.size());
        Unit unit = UnitFactory::create(static_cast<UnitType>(v[4]), static_cast<PlayerId>(v[3]), pos);
        unit.setId(unitId); unit.setHealth(v[2]); unit.setMaxHealth(v[21]);
        if (v[22] >= 0) unit.setOriginCityId(static_cast<CityId>(v[22]));

        const UnitId sourceId = sourceMap.unitOn(pos);
        if (static_cast<PlayerId>(v[3]) == perspective && sourceId != Map::kNoUnit) {
            if (const Unit* sourceUnit = observedSource.getUnit(sourceId);
                sourceUnit && sourceUnit->getOwnerId() == perspective &&
                static_cast<int>(sourceUnit->getType()) == v[4] &&
                sourceUnit->getHealth() == v[2] && sourceUnit->getMaxHealth() == v[21]) {
                unit = *sourceUnit;
                unit.setId(unitId); unit.setPos(pos);
            }
        }
        belief.units.push_back(unit);
        belief.map.setUnitOn(pos, unitId);
        belief.players[static_cast<size_t>(v[3])].addUnit(unitId);
        if (v[22] >= 0) belief.cities[static_cast<size_t>(v[22])].addUnit(unitId);
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
