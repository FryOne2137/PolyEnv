#ifndef GAME_ENGINE_ACTIONSPACE_H
#define GAME_ENGINE_ACTIONSPACE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "Action.h"

class Game;

// Stable action-id mapping for a fixed board size (W x H).
// IDs are grouped by action type and can be encoded/decoded deterministically.
class ActionSpace {
public:
    ActionSpace(int width, int height);

    size_t size() const { return totalSize_; }
    int width() const { return width_; }
    int height() const { return height_; }

    std::optional<size_t> encode(const Game& game, const Action& action) const;
    std::optional<Action> decode(const Game& game, size_t actionId) const;

    std::vector<uint8_t> legalMask(const Game& game, const std::vector<Action>& legalActions) const;

private:
    static int idxForTileAction(Action::TileActionKind kind);
    static Action::TileActionKind tileActionFromIdx(int idx);

    static int idxForUnitUpgrade(Action::UnitUpgradeKind kind);
    static Action::UnitUpgradeKind unitUpgradeFromIdx(int idx);

    static int idxForBuilding(BuildingTypeEnum b);
    static BuildingTypeEnum buildingFromIdx(int idx);

    static int idxForCityUpgrade(CityUpgradeChoice c);
    static CityUpgradeChoice cityUpgradeFromIdx(int idx);

    int posToIndex(Pos p) const;
    Pos indexToPos(int idx) const;
    bool validPos(Pos p) const;

    int width_ = 0;
    int height_ = 0;
    int cellCount_ = 0;

    size_t offEndTurn_ = 0;
    size_t offBuyTech_ = 0;
    size_t offMove_ = 0;
    size_t offAttack_ = 0;
    size_t offHeal_ = 0;
    size_t offUnitUpgrade_ = 0;
    size_t offTileAction_ = 0;
    size_t offBuild_ = 0;
    size_t offUpgradeCity_ = 0;
    size_t totalSize_ = 0;

    static constexpr int kTechCount = static_cast<int>(TechDB::TECH_COUNT);
    static constexpr int kTileActionCount = 11;   // TileActionKind without None
    static constexpr int kUnitUpgradeCount = 4;   // UnitUpgradeKind without None
    static constexpr int kBuildingCount = 16;     // All non-None building ids
    static constexpr int kCityUpgradeCount = 8;   // CityUpgradeChoice without None
};

#endif // GAME_ENGINE_ACTIONSPACE_H
