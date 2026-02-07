//
// Created by Fryderyk Niedzwiecki on 03/02/2026.
//

#include "MonumentSystem.h"

#include <cstddef>

#include "Game.h"
#include "Systems/CitySystem.h"
#include "Systems/PlayerSystem.h"
#include "terrain/BuildingTypeEnum.h"
#include "World/Map.h"
#include "World/Pos.h"
#include "tech/TechDB.h"

static bool isMonument(BuildingTypeEnum t) {
    switch (t) {
        case BuildingTypeEnum::AltarOfPeace:
        case BuildingTypeEnum::EmperorsTomb:
        case BuildingTypeEnum::EyeOfGod:
        case BuildingTypeEnum::GateOfPower:
        case BuildingTypeEnum::GrandBazaar:
        case BuildingTypeEnum::ParkOfFortune:
        case BuildingTypeEnum::TowerOfWisdom:
            return true;
        default:
            return false;
    }
}

void MonumentSystem::onConnectedCitiesUpdated(Game& game, PlayerId pid, size_t connectedNonCapitalCities) {
    if (connectedNonCapitalCities < 5) return;
    if (!PlayerSystem::playerExists(game, pid)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::GrandBazaar);
}

void MonumentSystem::onCityReachedLevel5(Game& game, Pos pos) {
    if (!game.getMap().inBounds(pos)) return;

    const Tile& t = game.getMap().at(pos);
    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return;

    if (!CitySystem::cityExists(game, cid)) return;

    // ✅ Safety: only award when the city is actually level 5+
    if (CitySystem::getCityLevel(game, cid) < 5) return;

    const PlayerId pid = static_cast<PlayerId>(CitySystem::getCityOwner(game, cid));
    if (!PlayerSystem::playerExists(game, pid)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::ParkOfFortune);
}


void MonumentSystem::onLighthouseCountUpdated(Game& game, PlayerId pid, int lighthouseCount) {
    if (lighthouseCount < 4) return;
    if (!PlayerSystem::playerExists(game, pid)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::EyeOfGod);
}

void MonumentSystem::onStarsUpdated(Game& game, PlayerId pid) {
    if (!PlayerSystem::playerExists(game, pid)) return;

    if (PlayerSystem::getStars(game, pid) < 100) return;

    // Emperor's Tomb requires Trade tech
    if (!PlayerSystem::hasTech(game, pid, TechId::Trade)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::EmperorsTomb);
}

void MonumentSystem::onKillsUpdated(Game& game, PlayerId pid, int totalKills) {
    if (totalKills < 10) return;
    if (!PlayerSystem::playerExists(game, pid)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::GateOfPower);
}

void MonumentSystem::onNoAttackTurnsUpdated(Game& game, PlayerId pid, int noAttackTurns) {
    if (noAttackTurns < 5) return;
    if (!PlayerSystem::playerExists(game, pid)) return;

    // Polytopia: Pacifist task is unlocked by Meditation tech.
    if (!PlayerSystem::hasTech(game, pid, TechId::Meditation)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::AltarOfPeace);
}

void MonumentSystem::onAllTechUnlockedUpdated(Game& game, PlayerId pid, bool allTechUnlocked) {
    if (!allTechUnlocked) return;
    if (!PlayerSystem::playerExists(game, pid)) return;

    (void)PlayerSystem::addMonument(game, pid, BuildingTypeEnum::TowerOfWisdom);
}


bool MonumentSystem::checkCityLevelForMonument(Game& game, CityId cid) {
    if (cid == kNoCity) return false;
    if (!CitySystem::cityExists(game, cid)) return false;
    return CitySystem::getCityLevel(game, cid) >= 5;
}
