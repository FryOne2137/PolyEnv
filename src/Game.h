//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#ifndef GAME_ENGINE_GAME_H
#define GAME_ENGINE_GAME_H

#include <cstdint>
#include <vector>
#include <array>

#include "World/Map.h"
#include "World/Pos.h"
#include "World/MapGenerator.h"

#include "Player/Player.h"
#include "units/Unit.h"
#include "tribes/Tribe.h"
#include "World/Settlements/City.h"
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
    struct NewGameConfig {
        int mapSize = 16;
        float initialLand = 0.5f;
        int smoothing = 3;
        int relief = 4;
        uint32_t seed = 0;

        std::vector<TribeType> tribes; // 2..16
    };

    Game() = default;

    // Tworzy nową grę: generuje mapę + tworzy graczy + ustawia turę 0
    void newGame(const NewGameConfig &cfg);

    bool buyTech(PlayerId pid, TechId tech);


    // ---- Tura / kolejność ----
    PlayerId getCurrentPlayerId() const { return currentPlayer; }
    Player &getCurrentPlayer() { return players[currentPlayer]; }
    const Player &getCurrentPlayer() const { return players[currentPlayer]; }
    uint32_t getTurnNumber() const { return turnNumber; }
    uint32_t getWorldSeed() const { return worldSeed; }

    uint16_t getLighthouseDiscoveredByMask(uint8_t lighthouseIdx) const;
    bool hasPlayerDiscoveredLighthouse(uint8_t lighthouseIdx, PlayerId pid) const;
    bool markLighthouseDiscovered(uint8_t lighthouseIdx, PlayerId pid); // true tylko przy pierwszym odkryciu

    bool endTurn(PlayerId pid); // przechodzi do następnego gracza
    bool handleRuin(UnitId unitId, Pos pos);
    bool handleStarfish(UnitId unitId, Pos pos);

    // ---- Dostęp do stanu (AI/UI) ----
    const Map &getMap() const { return map; }
    Map &getMap() { return map; }

    const std::vector<Player> &getPlayers() const { return players; }
    std::vector<Player> &getPlayers() { return players; }

    const std::vector<Unit> &getUnits() const { return units; }
    std::vector<Unit> &getUnits() { return units; }

    // ---- Akcje (walidacja kolejności tur) ----
    bool moveUnit(UnitId unitId, Pos to);

    bool attack(UnitId attackerId, Pos target);

    bool heal(UnitId healerId);

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

    bool foundCityFromVillage(PlayerId owner, Pos pos);
    bool captureCityAt(PlayerId newOwner, Pos pos);

    City* getCityBySettlementId(SettlementId sid);
    const City* getCityBySettlementId(SettlementId sid) const;
    bool buildBuilding(PlayerId builder, Pos pos, BuildingTypeEnum type);

    // ---- Tile actions (clear/hunt/fish/etc.) ----
    bool hunt(PlayerId pid, Pos pos);
    bool fishing(PlayerId pid, Pos pos);
    bool clearForest(PlayerId pid, Pos pos);
    bool burnForest(PlayerId pid, Pos pos);
    bool growForest(PlayerId pid, Pos pos);
    bool destroyTile(PlayerId pid, Pos pos);
    bool organization(PlayerId pid, Pos pos);

    bool canUpgradeRaftToScout(UnitId unitId) const;
    bool canUpgradeRaftToRammer(UnitId unitId) const;
    bool canUpgradeRaftToBomber(UnitId unitId) const;

    bool upgradeRaftToScout(UnitId unitId);
    bool upgradeRaftToRammer(UnitId unitId);
    bool upgradeRaftToBomber(UnitId unitId);

    bool canUnitBecomeVeteran(UnitId unitId) const;
    bool becomeVeteran(UnitId unitId);
    std::vector<Pos> attackable(UnitId attackerId) const;
    std::vector<Pos> reachable(UnitId unitId) const;

    bool attack(PlayerId pid, UnitId attackerId, Pos target);

    bool buildRoad(PlayerId pid, Pos pos);
    bool buildBridge(PlayerId pid, Pos pos);

    bool explorer(PlayerId pid, Pos start);
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
};

#endif // GAME_ENGINE_GAME_H
