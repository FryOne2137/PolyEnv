//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_GAME_H
#define GAME_ENGINE_GAME_H

#include <cstdint>
#include <vector>
#include <array>
#include <deque>

#include "world/Map.h"
#include "world/Pos.h"
#include "world/MapGenerator.h"

#include "player/Player.h"
#include "systems/CityRewardSystem.h"
#include "content/units/Unit.h"
#include "content/tribes/Tribe.h"
#include "content/settlements/City.h"
#include "terrain/BuildingTypeEnum.h"

// Forward declarations systemów (żeby Game.h nie robił ciężkich include)
class MovementSystem;
class CombatSystem;
class TurnSystem;
class InteractionSystem;
class VisionSystem;
class City;

class Game {
public:

    enum class MapCorner : uint8_t {
        TopLeft = 0,
        BottomLeft = 1,
        TopRight = 2,
        BottomRight = 3,
    };


    struct NewGameConfig {
        int mapSize = 16;
        float initialLand = 0.5f;
        int smoothing = 3;
        int relief = 4;
        uint32_t seed = 0;

        std::vector<TribeType> tribes; // 2..16
    };

    Game() = default;

    // ---- Pending City Upgrades (modal reward selection) ----
    struct PendingCityUpgrade {
        PlayerId pid;
        CityId cityId;
        CityUpgradeOptions opts;
        uint32_t createdOnTurn;
    };

    // Returns true if this player must resolve a city upgrade reward before doing anything else
    bool hasPendingCityUpgrade(PlayerId pid) const;

    // Returns the next pending upgrade for this player (nullptr if none)
    const PendingCityUpgrade* peekPendingCityUpgrade(PlayerId pid) const;

    // Resolves the next pending upgrade for this player (must match front of queue)
    bool resolvePendingCityUpgrade(PlayerId pid, CityUpgradeChoice choice);
    bool enqueuePendingCityUpgrade(PlayerId pid, CityId cityId);

    // Tworzy nową grę: generuje mapę + tworzy graczy + ustawia turę 0
    void newGame(const NewGameConfig &cfg);

    bool buyTech(PlayerId pid, TechId tech);
    bool canBuyTech(PlayerId pid, TechId tech) const;


    // ---- Tura / kolejność ----
    PlayerId getCurrentPlayerId() const { return currentPlayer; }
    Player &getCurrentPlayer() { return players[currentPlayer]; }
    const Player &getCurrentPlayer() const { return players[currentPlayer]; }
    uint32_t getTurnNumber() const { return turnNumber; }
    uint32_t getWorldSeed() const { return worldSeed; }
    bool isGameOver() const { return winner != kNoPlayer; }
    PlayerId getWinner() const { return winner; }

    uint16_t getLighthouseDiscoveredByMask(uint8_t lighthouseIdx) const;
    bool hasPlayerDiscoveredLighthouse(uint8_t lighthouseIdx, PlayerId pid) const;
    bool markLighthouseDiscovered(uint8_t lighthouseIdx, PlayerId pid); // true tylko przy pierwszym odkryciu

    bool endTurn(PlayerId pid); // przechodzi do następnego gracza
    bool handleRuin(PlayerId pid, UnitId unitId, Pos pos);
    bool canHandleRuin(PlayerId pid, UnitId unitId, Pos pos) const;
    bool handleStarfish(PlayerId pid, UnitId unitId, Pos pos);
    bool canHandleStarfish(PlayerId pid, UnitId unitId, Pos pos) const;
    bool handleCityCapture(PlayerId pid, UnitId unitId, Pos pos);
    bool canHandleCityCapture(PlayerId pid, UnitId unitId, Pos pos) const;


    bool hasPlayerSeenCorner(PlayerId pid, MapCorner corner) const;

    // ---- Dostęp do stanu (AI/UI) ----
    const Map &getMap() const { return map; }
    Map &getMap() { return map; }

    const std::vector<Player> &getPlayers() const { return players; }
    std::vector<Player> &getPlayers() { return players; }

    const std::vector<Unit> &getUnits() const { return units; }
    std::vector<Unit> &getUnits() { return units; }

    // ---- Akcje (walidacja kolejności tur) ----
    bool moveUnit(PlayerId pid, UnitId unitId, Pos to);
    bool canMoveUnit(PlayerId pid, UnitId unitId, Pos to) const;
    bool attack(PlayerId pid, UnitId attackerId, Pos target);
    bool canAttack(PlayerId pid, UnitId attackerId, Pos target) const;
    bool heal(PlayerId pid, UnitId healerId);
    bool canHeal(PlayerId pid, UnitId healerId) const;

    std::vector<Pos> attackable(PlayerId pid, UnitId attackerId) const;
    std::vector<Pos> reachable(PlayerId pid, UnitId unitId) const;

    // Fabryka jednostek w świecie (na razie minimalnie)
    UnitId spawnUnit(UnitType type, PlayerId owner, Pos pos, bool canActImmediately=false);

    std::vector<BuildingTypeEnum> getPlayerEarnedMonuments(PlayerId pid) const;
    std::vector<BuildingTypeEnum> getPlayerPlacedMonuments(PlayerId pid) const;
    std::vector<BuildingTypeEnum> getPlayerOwnedMonuments(PlayerId pid) const;

    // ---- Helpers for systems ----
    Player &getPlayer(PlayerId id) { return players[id]; }
    const Player &getPlayer(PlayerId id) const { return players[id]; }

    Unit *getUnit(UnitId id);

    const Unit *getUnit(UnitId id) const;

    // Cities are stored centrally; tiles reference them by CityId / SettlementId.
    City *getCity(CityId id);

    const City *getCity(CityId id) const;

    const std::vector<City> &getCities() const { return cities; }
    std::vector<City> &getCities() { return cities; }

    int getPlayerScore(PlayerId pid) const;

    bool foundCityFromVillage(PlayerId pid, Pos pos);
    bool canFoundCityFromVillage(PlayerId pid, Pos pos) const;

    bool canUpgradeCity(PlayerId pid, CityId cityId) const;

    CityUpgradeOptions getCityUpgradeOptions(PlayerId pid, CityId cityId) const;

    bool upgradeCity(PlayerId pid, CityId cityId, CityUpgradeChoice choice);

    bool captureCityAt(PlayerId pid, Pos pos);
    bool canCaptureCityAt(PlayerId pid, Pos pos) const;

    bool buildBuilding(PlayerId builder, Pos pos, BuildingTypeEnum type);
    bool canBuildBuilding(PlayerId builder, Pos pos, BuildingTypeEnum type) const;

    // ---- Tile actions (clear/hunt/fish/etc.) ----
    bool hunt(PlayerId pid, Pos pos);
    bool canHunt(PlayerId pid, Pos pos) const;
    bool fishing(PlayerId pid, Pos pos);
    bool canFishing(PlayerId pid, Pos pos) const;
    bool clearForest(PlayerId pid, Pos pos);
    bool canClearForest(PlayerId pid, Pos pos) const;
    bool burnForest(PlayerId pid, Pos pos);
    bool canBurnForest(PlayerId pid, Pos pos) const;
    bool growForest(PlayerId pid, Pos pos);
    bool canGrowForest(PlayerId pid, Pos pos) const;
    bool destroyTile(PlayerId pid, Pos pos);
    bool canDestroyTile(PlayerId pid, Pos pos) const;
    bool organization(PlayerId pid, Pos pos);
    bool canOrganization(PlayerId pid, Pos pos) const;

    bool canUpgradeRaftToScout(PlayerId pid, UnitId unitId) const;
    bool canUpgradeRaftToRammer(PlayerId pid, UnitId unitId) const;
    bool canUpgradeRaftToBomber(PlayerId pid, UnitId unitId) const;

    bool upgradeRaftToScout(PlayerId pid, UnitId unitId);
    bool upgradeRaftToRammer(PlayerId pid, UnitId unitId);
    bool upgradeRaftToBomber(PlayerId pid, UnitId unitId);

    bool canUnitBecomeVeteran(PlayerId pid, UnitId unitId) const;
    bool becomeVeteran(PlayerId pid, UnitId unitId);

    bool buildRoad(PlayerId pid, Pos pos);
    bool canBuildRoad(PlayerId pid, Pos pos) const;
    bool buildBridge(PlayerId pid, Pos pos);
    bool canBuildBridge(PlayerId pid, Pos pos) const;

    bool explorer(PlayerId pid, Pos start);
    bool canExplorer(PlayerId pid, Pos start) const;
    bool isPlayersTurn(PlayerId pid) const { return pid == currentPlayer; };

private:
    friend class UnitSpawnSystem;

    uint32_t worldSeed = 1;

    Map map;

    std::vector<Player> players;
    std::vector<Unit> units;
    std::vector<City> cities;

    PlayerId currentPlayer = 0;
    uint32_t turnNumber = 0;

    std::array<uint16_t, 4> lighthouseDiscoveredBy = {0, 0, 0, 0};

    std::deque<PendingCityUpgrade> pendingCityUpgrades;
    PlayerId winner = kNoPlayer;

    void updateWinnerByCapitals();
};

#endif // GAME_ENGINE_GAME_H
