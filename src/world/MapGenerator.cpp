//
// Polytopia-like Lakes map generation.
//

#include "MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

constexpr int kMinSuburbs = 1;
constexpr int kMaxSuburbs = 2;
constexpr float kPreTerrainVillageDensity = 0.30f;

// Lakes terrain tuning. These are intentionally grouped here so the map
// character can be adjusted without exposing runtime sliders.
constexpr int kLakesSmoothingPasses = 2;
constexpr bool kLakesDryMapEdges = true;
constexpr float kLakesDryEdgeValue = 1.0f;
constexpr float kLakesMinWaterFraction = 0.25f;
constexpr float kLakesMaxBaseWaterFraction = 0.28f;

// The documented 50% fish rate applies only to shallow water.  Ocean fish
// are allowed near a city's territory (and its immediate outer ring), but at
// a deliberately lower density.  The public documentation does not define a
// numeric ocean rate, so keep this separate and easy to tune.
constexpr float kShallowFishSpawnChance = 0.50f;
constexpr float kOceanFishSpawnChance = 0.10f;

bool isWater(BaseTerrainEnum terrain) {
    return terrain == BaseTerrainEnum::Water || terrain == BaseTerrainEnum::Ocean;
}

int ruinTargetForMapSize(int tileCount) {
    switch (tileCount) {
        case 121: return 4;  // Tiny, 11x11
        case 196: return 5;  // Small, 14x14
        case 256: return 7;  // Normal, 16x16
        case 324: return 9;  // Large, 18x18
        case 400: return 11; // Huge, 20x20
        case 900: return 23; // Massive, 30x30
        default: return std::max(1, static_cast<int>(std::lround(tileCount * 23.0 / 900.0)));
    }
}

} // namespace

int MapGenerator::randInt(std::mt19937& rng, int a, int b) {
    std::uniform_int_distribution<int> distribution(a, b);
    return distribution(rng);
}

float MapGenerator::rand01(std::mt19937& rng) {
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    return distribution(rng);
}

int MapGenerator::chebyshevDistance(int a, int b, int size) {
    return std::max(std::abs(a % size - b % size), std::abs(a / size - b / size));
}

std::vector<int> MapGenerator::circle(int center, int radius, int size) {
    std::vector<int> cells;
    const int cx = center % size;
    const int cy = center / size;
    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || y < 0 || x >= size || y >= size) continue;
            if (std::max(std::abs(x - cx), std::abs(y - cy)) == radius) {
                cells.push_back(y * size + x);
            }
        }
    }
    return cells;
}

std::vector<int> MapGenerator::round(int center, int radius, int size) {
    std::vector<int> cells;
    const int cx = center % size;
    const int cy = center / size;
    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x >= 0 && y >= 0 && x < size && y < size) cells.push_back(y * size + x);
        }
    }
    return cells;
}

std::vector<int> MapGenerator::plusSign(int center, int size) {
    std::vector<int> cells;
    const int x = center % size;
    const int y = center / size;
    if (x > 0) cells.push_back(center - 1);
    if (x + 1 < size) cells.push_back(center + 1);
    if (y > 0) cells.push_back(center - size);
    if (y + 1 < size) cells.push_back(center + size);
    return cells;
}

bool MapGenerator::isLandLike(BaseTerrainEnum terrain) {
    return terrain == BaseTerrainEnum::Land || terrain == BaseTerrainEnum::Forest ||
           terrain == BaseTerrainEnum::Mountain;
}

MapGenerator::TribeProbs MapGenerator::probsForTribe(TribeType tribe) {
    TribeProbs probabilities{};
    switch (tribe) {
        case TribeType::Kickoo: probabilities.fish = 1.5f; probabilities.mountain = 0.5f; break;
        case TribeType::Aquarion: probabilities.forest = 0.5f; break;
        case TribeType::Bardur: probabilities.forest = 0.8f; probabilities.crops = 0.0f; break;
        case TribeType::XinXi: probabilities.mountain = 1.5f; probabilities.metal = 1.5f; break;
        case TribeType::AiMo: probabilities.mountain = 1.5f; probabilities.crops = 0.1f; break;
        case TribeType::Oumaji: probabilities.forest = 0.2f; probabilities.mountain = 0.5f; probabilities.animals = 0.2f; break;
        case TribeType::Vengir: probabilities.metal = 2.0f; probabilities.animals = 0.1f; probabilities.fruit = 0.1f; probabilities.fish = 0.1f; break;
        case TribeType::Imperius: probabilities.fruit = 2.0f; probabilities.animals = 0.5f; break;
        case TribeType::Hoodrick: probabilities.forest = 1.5f; probabilities.mountain = 0.5f; break;
        case TribeType::Zebasi: probabilities.forest = 0.5f; probabilities.mountain = 0.5f; probabilities.fruit = 0.5f; break;
        case TribeType::Quetzali: probabilities.fruit = 2.0f; probabilities.crops = 0.1f; break;
        case TribeType::Yadakk: probabilities.forest = 0.5f; probabilities.mountain = 0.5f; probabilities.fruit = 1.5f; break;
        default: break;
    }
    return probabilities;
}

std::vector<Pos> MapGenerator::generate(Map& map, const Params& params) {
    const int size = params.mapSize;
    const int tileCount = size * size;
    const auto& tribes = map.getActiveTribes();
    if (size < 3 || tribes.size() < 2 || tribes.size() > 16) return {};
    if (map.getWidth() != size || map.getHeight() != size) map.init(size, size);

    std::mt19937 rng(params.seed == 0 ? std::random_device{}() : params.seed);
    auto& tiles = map.allTiles();
    const auto isCorner = [size](int cell) {
        return cell == 0 || cell == size - 1 || cell == size * (size - 1) || cell == size * size - 1;
    };
    for (Tile& tile : tiles) {
        tile.setBaseTerrain(BaseTerrainEnum::Ocean);
        tile.setResource(ResourcesEnum::None);
        tile.clearSettlement();
        tile.setBuildingType(BuildingTypeEnum::None);
        tile.setRoadBridge(RoadBridgeEnum::None);
        tile.setTribe(TribeType::Unknown);
        tile.clearTerritory();
    }

    const int players = static_cast<int>(tribes.size());
    const bool lakes = params.mapType == MapType::Lakes;
    const int domainsPerAxis = players <= 4 ? 2 : players <= 9 ? 3 : 4;
    std::vector<int> domains(domainsPerAxis * domainsPerAxis);
    std::iota(domains.begin(), domains.end(), 0);
    std::shuffle(domains.begin(), domains.end(), rng);

    // 1. Capitals are chosen before terrain, one in each map domain.
    std::vector<int> capitals(static_cast<size_t>(players));
    std::vector<uint8_t> reservedGround(static_cast<size_t>(tileCount), 0);
    for (int player = 0; player < players; ++player) {
        const int domain = domains[static_cast<size_t>(player)];
        const int dx = domain % domainsPerAxis;
        const int dy = domain / domainsPerAxis;
        const int x0 = (dx * size) / domainsPerAxis;
        const int x1 = ((dx + 1) * size) / domainsPerAxis;
        const int y0 = (dy * size) / domainsPerAxis;
        const int y1 = ((dy + 1) * size) / domainsPerAxis;
        const int centerX = (x0 + x1 - 1) / 2;
        const int centerY = (y0 + y1 - 1) / 2;
        // Capitals in Lakes are domain-based starts.  Select them from the
        // middle of the domain, rather than anywhere inside it, so each spawn
        // is visibly centred in its assigned quadrant.
        const int centerRadius = std::max(1, std::min(x1 - x0, y1 - y0) / 4);
        std::vector<int> candidates;
        if (players <= 4) {
            // Small games use corner starts.  A capital may be at most three
            // coordinates away from its domain's corner: for (0, 0), that is
            // the 3x3 range (1, 1)..(3, 3).  The corner itself is reserved as
            // empty ground by the final map invariant.
            const int minX = dx == 0 ? 1 : std::max(1, size - 4);
            const int maxX = dx == 0 ? std::min(3, size - 2) : size - 2;
            const int minY = dy == 0 ? 1 : std::max(1, size - 4);
            const int maxY = dy == 0 ? std::min(3, size - 2) : size - 2;
            const int cornerAdjacentX = dx == 0 ? minX : maxX;
            const int cornerAdjacentY = dy == 0 ? minY : maxY;
            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x) {
                    // Do not allow the tile diagonally adjacent to the corner
                    // (e.g. (1, 1) or (n-2, n-2)).  A unit starting in the
                    // capital must not reveal the corner immediately.
                    if (x == cornerAdjacentX && y == cornerAdjacentY) continue;
                    candidates.push_back(y * size + x);
                }
        } else {
            for (int y = std::max({1, y0, centerY - centerRadius});
                 y <= std::min({size - 2, y1 - 1, centerY + centerRadius}); ++y)
                for (int x = std::max({1, x0, centerX - centerRadius});
                     x <= std::min({size - 2, x1 - 1, centerX + centerRadius}); ++x)
                    candidates.push_back(y * size + x);
        }
        if (candidates.empty()) candidates.push_back(centerY * size + centerX);
        const int capital = candidates[static_cast<size_t>(randInt(rng, 0, static_cast<int>(candidates.size()) - 1))];
        capitals[static_cast<size_t>(player)] = capital;
        reservedGround[static_cast<size_t>(capital)] = 1;
    }

    // 2. Lakes has suburbs and pre-terrain villages; Drylands does not.
    std::vector<int> reservedCities = capitals;
    std::vector<int> suburbs;
    auto isFarFromCities = [&](int cell) {
        return std::all_of(reservedCities.begin(), reservedCities.end(), [&](int city) {
            return chebyshevDistance(cell, city, size) > 2;
        });
    };
    for (int capital : capitals) {
        if (!lakes) break;
        std::vector<int> candidates;
        for (int cell : round(capital, 4, size)) {
            const int x = cell % size, y = cell / size;
            if (x > 0 && y > 0 && x + 1 < size && y + 1 < size && isFarFromCities(cell)) candidates.push_back(cell);
        }
        std::shuffle(candidates.begin(), candidates.end(), rng);
        const int wanted = std::min(kMaxSuburbs, std::max(kMinSuburbs, static_cast<int>(candidates.size())));
        for (int i = 0; i < wanted && i < static_cast<int>(candidates.size()); ++i) {
            const int cell = candidates[static_cast<size_t>(i)];
            if (!isFarFromCities(cell)) continue;
            suburbs.push_back(cell);
            reservedCities.push_back(cell);
            reservedGround[static_cast<size_t>(cell)] = 1;
        }
    }

    // Wiki formula for Lakes:
    // floor(((mapWidth / 3)^2 - capitals - suburbs) * densityCoefficient).
    // Integer division deliberately rounds mapWidth / 3 down before squaring.
    const int gridSlots = (size / 3) * (size / 3);
    const int availablePreTerrainVillageSlots = std::max(0, gridSlots -
        static_cast<int>(capitals.size()) - static_cast<int>(suburbs.size()));
    const int preTerrainTarget = lakes ? static_cast<int>(std::floor(
        availablePreTerrainVillageSlots * kPreTerrainVillageDensity)) : 0;
    std::vector<int> preTerrainVillages;
    for (int i = 0; i < preTerrainTarget; ++i) {
        std::vector<int> candidates;
        for (int y = 1; y + 1 < size; ++y) for (int x = 1; x + 1 < size; ++x) {
            const int cell = y * size + x;
            if (isFarFromCities(cell)) candidates.push_back(cell);
        }
        if (candidates.empty()) break;
        const int cell = candidates[static_cast<size_t>(randInt(rng, 0, static_cast<int>(candidates.size()) - 1))];
        preTerrainVillages.push_back(cell);
        reservedCities.push_back(cell);
        reservedGround[static_cast<size_t>(cell)] = 1;
    }

    // 3. Lakes starts as a random floating-point field. After local smoothing,
    // its lowest values become the binary water mask.
    std::vector<uint8_t> ground(static_cast<size_t>(tileCount), 1);
    if (lakes) {
        const int minWaterTiles = static_cast<int>(std::ceil(tileCount * kLakesMinWaterFraction));
        const int maxWaterTiles = static_cast<int>(std::floor(tileCount * kLakesMaxBaseWaterFraction));
        std::vector<uint8_t> lockedLand = reservedGround;
        for (int capital : capitals) for (int neighbour : plusSign(capital, size)) {
            lockedLand[static_cast<size_t>(neighbour)] = 1;
        }
        std::vector<float> lakeField(static_cast<size_t>(tileCount));
        for (float& value : lakeField) value = rand01(rng);
        for (int cell = 0; cell < tileCount; ++cell) if (lockedLand[static_cast<size_t>(cell)]) {
            lakeField[static_cast<size_t>(cell)] = kLakesDryEdgeValue;
        }

        for (int pass = 0; pass < kLakesSmoothingPasses; ++pass) {
            std::vector<float> next(static_cast<size_t>(tileCount), kLakesDryEdgeValue);
            for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
                const int cell = y * size + x;
                if (lockedLand[static_cast<size_t>(cell)]) continue;
                float sum = 0.0f;
                int sampleCount = 0;
                for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (nx < 0 || ny < 0 || nx >= size || ny >= size) {
                        if (kLakesDryMapEdges) {
                            sum += kLakesDryEdgeValue;
                            ++sampleCount;
                        }
                        continue;
                    }
                    sum += lakeField[static_cast<size_t>(ny * size + nx)];
                    ++sampleCount;
                }
                next[static_cast<size_t>(cell)] = sum / sampleCount;
            }
            lakeField.swap(next);
        }

        std::vector<int> waterCandidates;
        waterCandidates.reserve(static_cast<size_t>(tileCount));
        for (int cell = 0; cell < tileCount; ++cell) {
            if (!lockedLand[static_cast<size_t>(cell)]) waterCandidates.push_back(cell);
        }
        std::sort(waterCandidates.begin(), waterCandidates.end(), [&](int left, int right) {
            return lakeField[static_cast<size_t>(left)] < lakeField[static_cast<size_t>(right)];
        });
        const int targetWaterTiles = randInt(rng, minWaterTiles, maxWaterTiles);
        std::vector<uint8_t> waterMask(static_cast<size_t>(tileCount), 0);
        for (int index = 0; index < targetWaterTiles && index < static_cast<int>(waterCandidates.size()); ++index) {
            waterMask[static_cast<size_t>(waterCandidates[static_cast<size_t>(index)])] = 1;
        }
        for (int cell = 0; cell < tileCount; ++cell) {
            ground[static_cast<size_t>(cell)] = !waterMask[static_cast<size_t>(cell)];
        }
    }
    for (int cell = 0; cell < tileCount; ++cell) if (reservedGround[static_cast<size_t>(cell)]) {
        ground[static_cast<size_t>(cell)] = 1;
    }
    for (int capital : capitals) for (int neighbor : plusSign(capital, size)) {
        ground[static_cast<size_t>(neighbor)] = 1;
    }

    for (int cell = 0; cell < tileCount; ++cell) {
        tiles[static_cast<size_t>(cell)].setBaseTerrain(
            ground[static_cast<size_t>(cell)] ? BaseTerrainEnum::Land : BaseTerrainEnum::Ocean);
    }
    for (int player = 0; player < players; ++player) {
        Tile& tile = tiles[static_cast<size_t>(capitals[static_cast<size_t>(player)])];
        tile.setSettlement(SettlementTypeEnum::City, static_cast<SettlementId>(player));
        tile.setRoadBridge(RoadBridgeEnum::Road);
    }
    SettlementId nextSettlement = static_cast<SettlementId>(players);
    for (int cell : suburbs) tiles[static_cast<size_t>(cell)].setSettlement(SettlementTypeEnum::Village, nextSettlement++);
    for (int cell : preTerrainVillages) tiles[static_cast<size_t>(cell)].setSettlement(SettlementTypeEnum::Village, nextSettlement++);

    // 6. Post-terrain villages are selected only after water has been established.
    const int postTerrainTarget = std::max(0, gridSlots - static_cast<int>(capitals.size()) -
        static_cast<int>(preTerrainVillages.size()));
    for (int placed = 0; placed < postTerrainTarget; ++placed) {
        std::vector<int> candidates;
        for (int y = 2; y + 2 < size; ++y) for (int x = 2; x + 2 < size; ++x) {
            const int cell = y * size + x;
            if (tiles[static_cast<size_t>(cell)].getBaseTerrain() != BaseTerrainEnum::Land) continue;
            if (isFarFromCities(cell)) candidates.push_back(cell);
        }
        if (candidates.empty()) break;
        const int cell = candidates[static_cast<size_t>(randInt(rng, 0, static_cast<int>(candidates.size()) - 1))];
        tiles[static_cast<size_t>(cell)].setSettlement(SettlementTypeEnum::Village, nextSettlement++);
        reservedCities.push_back(cell);
    }

    // 7. Assign biomes by alternating four-directional border growth.  Each
    // claimant chooses from its entire current border, so the eventual seams
    // are irregular without relying on distance-to-capital regions.
    std::vector<int> landOwner(static_cast<size_t>(tileCount), -1);
    std::vector<std::vector<int>> borderCells(static_cast<size_t>(players));
    int assignedLand = 0;
    int landCount = 0;
    for (int cell = 0; cell < tileCount; ++cell) {
        if (tiles[static_cast<size_t>(cell)].getBaseTerrain() == BaseTerrainEnum::Land) ++landCount;
    }
    for (int player = 0; player < players; ++player) {
        const int capital = capitals[static_cast<size_t>(player)];
        landOwner[static_cast<size_t>(capital)] = player;
        borderCells[static_cast<size_t>(player)].push_back(capital);
        ++assignedLand;
    }

    while (assignedLand < landCount) {
        bool expanded = false;
        std::vector<int> turnOrder(static_cast<size_t>(players));
        std::iota(turnOrder.begin(), turnOrder.end(), 0);
        std::shuffle(turnOrder.begin(), turnOrder.end(), rng);
        for (int player : turnOrder) {
            auto& border = borderCells[static_cast<size_t>(player)];
            std::vector<int> candidates;
            for (int source : border) for (int neighbor : plusSign(source, size)) {
                if (landOwner[static_cast<size_t>(neighbor)] == -1 &&
                    tiles[static_cast<size_t>(neighbor)].getBaseTerrain() == BaseTerrainEnum::Land) {
                    candidates.push_back(neighbor);
                }
            }
            if (candidates.empty()) continue;
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            const int chosen = candidates[static_cast<size_t>(randInt(rng, 0, static_cast<int>(candidates.size()) - 1))];
            landOwner[static_cast<size_t>(chosen)] = player;
            border.push_back(chosen);
            ++assignedLand;
            expanded = true;
        }
        if (expanded) continue;

        // A disconnected island has no land path to a claimant. Assign its
        // first tile to the closest capital instead of a random tribe, so a
        // remote island cannot receive an unrelated biome.
        std::vector<int> unassigned;
        for (int cell = 0; cell < tileCount; ++cell) {
            if (tiles[static_cast<size_t>(cell)].getBaseTerrain() == BaseTerrainEnum::Land &&
                landOwner[static_cast<size_t>(cell)] == -1) unassigned.push_back(cell);
        }
        if (unassigned.empty()) break;
        const int chosen = unassigned[static_cast<size_t>(randInt(rng, 0, static_cast<int>(unassigned.size()) - 1))];
        int player = 0;
        int closestDistance = chebyshevDistance(chosen, capitals[0], size);
        for (int candidate = 1; candidate < players; ++candidate) {
            const int distance = chebyshevDistance(chosen, capitals[static_cast<size_t>(candidate)], size);
            if (distance < closestDistance) {
                closestDistance = distance;
                player = candidate;
            }
        }
        landOwner[static_cast<size_t>(chosen)] = player;
        borderCells[static_cast<size_t>(player)].push_back(chosen);
        ++assignedLand;
    }

    // Turn already claimed land into its final terrain appearance.
    for (int cell = 0; cell < tileCount; ++cell) {
        Tile& tile = tiles[static_cast<size_t>(cell)];
        if (tile.getBaseTerrain() != BaseTerrainEnum::Land) continue;
        const int owner = landOwner[static_cast<size_t>(cell)];
        if (owner < 0) continue;
        tile.setTribe(tribes[static_cast<size_t>(owner)]);
        if (tile.getSettlementType() != SettlementTypeEnum::None) continue;
        const TribeProbs probabilities = probsForTribe(tile.getTribe());
        // Standard terrain shares are 14% mountains, 38% forest and 48%
        // fields. A mountain modifier redistributes its difference between
        // forest and field before the forest modifier is applied.
        constexpr float kBaseMountain = 0.14f;
        constexpr float kBaseForest = 0.38f;
        constexpr float kBaseField = 0.48f;
        const float mountain = std::clamp(kBaseMountain * probabilities.mountain, 0.0f, 1.0f);
        const float forestBeforeModifier = kBaseForest + (kBaseMountain - mountain) *
            (kBaseForest / (kBaseForest + kBaseField));
        const float forest = std::clamp(forestBeforeModifier * probabilities.forest, 0.0f, 1.0f - mountain);
        const float roll = rand01(rng);
        if (roll < mountain) tile.setBaseTerrain(BaseTerrainEnum::Mountain);
        else if (roll < mountain + forest) tile.setBaseTerrain(BaseTerrainEnum::Forest);
    }

    auto makeOcean = [&] {
        std::vector<uint8_t> shallow(static_cast<size_t>(tileCount), 0);
        for (int cell = 0; cell < tileCount; ++cell) {
            if (!isWater(tiles[static_cast<size_t>(cell)].getBaseTerrain())) continue;
            for (int neighbor : plusSign(cell, size)) if (isLandLike(tiles[static_cast<size_t>(neighbor)].getBaseTerrain())) { shallow[static_cast<size_t>(cell)] = 1; break; }
        }
        for (int cell = 0; cell < tileCount; ++cell) if (isWater(tiles[static_cast<size_t>(cell)].getBaseTerrain())) {
            tiles[static_cast<size_t>(cell)].setBaseTerrain(shallow[static_cast<size_t>(cell)] ? BaseTerrainEnum::Water : BaseTerrainEnum::Ocean);
            tiles[static_cast<size_t>(cell)].setTribe(TribeType::Unknown);
        }
    };
    makeOcean();

    // 8. AddResources. Resources occur only within two tiles of a city or
    // village. The adjacent ring is the inner-city area and uses the higher
    // spawn rates from the map-generation table.
    std::vector<uint8_t> resourceArea(static_cast<size_t>(tileCount), 0);
    std::vector<uint8_t> innerResourceArea(static_cast<size_t>(tileCount), 0);
    for (int city : reservedCities) {
        for (int cell : round(city, 2, size)) resourceArea[static_cast<size_t>(cell)] = 1;
        for (int cell : round(city, 1, size)) innerResourceArea[static_cast<size_t>(cell)] = 1;
    }
    const auto resourceProbabilities = [&](int cell, const Tile& tile) {
        if (tile.getTribe() != TribeType::Unknown) return probsForTribe(tile.getTribe());

        // Water has no displayed tribe. Use the nearest generated land biome
        // so shore fish retain the fish modifier of their local climate.
        int closestOwner = -1;
        int closestDistance = size * 2;
        for (int candidate = 0; candidate < tileCount; ++candidate) {
            const int owner = landOwner[static_cast<size_t>(candidate)];
            if (owner < 0) continue;
            const int distance = chebyshevDistance(cell, candidate, size);
            if (distance < closestDistance) {
                closestDistance = distance;
                closestOwner = owner;
            }
        }
        return closestOwner >= 0 ? probsForTribe(tribes[static_cast<size_t>(closestOwner)]) : TribeProbs{};
    };
    for (int cell = 0; cell < tileCount; ++cell) {
        Tile& tile = tiles[static_cast<size_t>(cell)];
        // The 50% fish rate applies to shallow water only. Ocean tiles in the
        // same resource area are also candidates, but with a lower rate.
        const bool shallowWaterTile = tile.getBaseTerrain() == BaseTerrainEnum::Water;
        const bool oceanTile = tile.getBaseTerrain() == BaseTerrainEnum::Ocean;
        if (isCorner(cell) || tile.getSettlementType() != SettlementTypeEnum::None ||
            !resourceArea[static_cast<size_t>(cell)]) continue;
        const TribeProbs probabilities = resourceProbabilities(cell, tile);
        const bool innerCity = innerResourceArea[static_cast<size_t>(cell)] != 0;
        const float roll = rand01(rng);
        if (tile.getBaseTerrain() == BaseTerrainEnum::Land) {
            const float fruit = (innerCity ? 0.18f : 0.06f) * probabilities.fruit;
            const float crops = (innerCity ? 0.18f : 0.06f) * probabilities.crops;
            if (roll < fruit) tile.setResource(ResourcesEnum::Fruit);
            else if (roll < fruit + crops) tile.setResource(ResourcesEnum::Crops);
        } else if (tile.getBaseTerrain() == BaseTerrainEnum::Forest &&
                   roll < (innerCity ? 0.19f : 0.06f) * probabilities.animals) {
            tile.setResource(ResourcesEnum::Animal);
        } else if (tile.getBaseTerrain() == BaseTerrainEnum::Mountain &&
                   roll < (innerCity ? 0.11f : 0.03f) * probabilities.metal) {
            tile.setResource(ResourcesEnum::Metal);
        } else if (shallowWaterTile && roll < kShallowFishSpawnChance * probabilities.fish) {
            tile.setResource(ResourcesEnum::Fish);
        } else if (oceanTile && roll < kOceanFishSpawnChance * probabilities.fish) {
            tile.setResource(ResourcesEnum::Fish);
        }
    }

    // 9. Guaranteed opening resources are added after normal resource generation.
    auto ensureCapitalResource = [&](int player, ResourcesEnum resource, BaseTerrainEnum terrain, int count) {
        int present = 0;
        for (int cell : round(capitals[static_cast<size_t>(player)], 1, size)) {
            const Tile& tile = tiles[static_cast<size_t>(cell)];
            if (tile.getBaseTerrain() == terrain && tile.getResource() == resource) ++present;
        }
        for (int cell : round(capitals[static_cast<size_t>(player)], 1, size)) {
            if (present >= count) break;
            Tile& tile = tiles[static_cast<size_t>(cell)];
            if (tile.getSettlementType() != SettlementTypeEnum::None) continue;
            tile.setBaseTerrain(terrain); tile.setResource(resource);
            tile.setTribe(terrain == BaseTerrainEnum::Water ? TribeType::Unknown : tribes[static_cast<size_t>(player)]);
            ++present;
        }
    };
    for (int player = 0; player < players; ++player) switch (tribes[static_cast<size_t>(player)]) {
        case TribeType::Bardur: ensureCapitalResource(player, ResourcesEnum::Animal, BaseTerrainEnum::Forest, 2); break;
        case TribeType::Imperius: ensureCapitalResource(player, ResourcesEnum::Fruit, BaseTerrainEnum::Land, 2); break;
        case TribeType::Kickoo: ensureCapitalResource(player, ResourcesEnum::Fish, BaseTerrainEnum::Water, 2); break;
        case TribeType::Zebasi: ensureCapitalResource(player, ResourcesEnum::Crops, BaseTerrainEnum::Land, 1); break;
        default: break;
    }
    // Drylands is land-only except for the two fish tiles guaranteed next to
    // Kickoo and Aquarion capitals.
    if (!lakes) {
        for (int player = 0; player < players; ++player) {
            if (tribes[static_cast<size_t>(player)] == TribeType::Aquarion) {
                ensureCapitalResource(player, ResourcesEnum::Fish, BaseTerrainEnum::Water, 2);
            }
        }
    }
    makeOcean();

    // 10. Ruins and starfish are the final map objects.
    std::vector<int> ruinCandidates;
    for (int cell = 0; cell < tileCount; ++cell) {
        if (!isCorner(cell) && tiles[static_cast<size_t>(cell)].getSettlementType() == SettlementTypeEnum::None) {
            ruinCandidates.push_back(cell);
        }
    }
    std::shuffle(ruinCandidates.begin(), ruinCandidates.end(), rng);
    const int ruinTarget = ruinTargetForMapSize(tileCount);
    int ruins = 0;
    for (int cell : ruinCandidates) {
        if (ruins >= ruinTarget) break;
        bool nearObject = false;
        for (int neighbor : round(cell, 2, size)) if (tiles[static_cast<size_t>(neighbor)].getSettlementType() != SettlementTypeEnum::None) { nearObject = true; break; }
        if (!nearObject) { tiles[static_cast<size_t>(cell)].setSettlement(SettlementTypeEnum::Ruin, nextSettlement++); ++ruins; }
    }
    std::vector<int> waterCandidates;
    int waterTileCount = 0;
    for (int cell = 0; cell < tileCount; ++cell) {
        const Tile& tile = tiles[static_cast<size_t>(cell)];
        if (isWater(tile.getBaseTerrain())) ++waterTileCount;
        if (!isCorner(cell) && isWater(tile.getBaseTerrain()) &&
            tile.getSettlementType() == SettlementTypeEnum::None &&
            tile.getResource() == ResourcesEnum::None) {
            waterCandidates.push_back(cell);
        }
    }
    std::shuffle(waterCandidates.begin(), waterCandidates.end(), rng);
    const int starfishTarget = waterTileCount / 25;
    int starfish = 0;
    for (int cell : waterCandidates) {
        if (starfish >= starfishTarget) break;
        bool adjacent = false;
        for (int neighbor : round(cell, 1, size)) if (tiles[static_cast<size_t>(neighbor)].getSettlementType() != SettlementTypeEnum::None) { adjacent = true; break; }
        if (!adjacent) { tiles[static_cast<size_t>(cell)].setSettlement(SettlementTypeEnum::Starfish, nextSettlement++); ++starfish; }
    }

    // Lakes keeps the four corners as empty ground.  This final invariant also
    // protects them from late passes such as guaranteed start resources.
    for (int corner : {0, size - 1, size * (size - 1), size * size - 1}) {
        Tile& tile = tiles[static_cast<size_t>(corner)];
        tile.setBaseTerrain(BaseTerrainEnum::Land);
        tile.setResource(ResourcesEnum::None);
        tile.clearSettlement();
        tile.setBuildingType(BuildingTypeEnum::None);
        tile.setRoadBridge(RoadBridgeEnum::None);
    }

    std::vector<Pos> capitalPositions;
    capitalPositions.reserve(static_cast<size_t>(players));
    for (int cell : capitals) capitalPositions.push_back(Pos{cell % size, cell / size});
    return capitalPositions;
}
