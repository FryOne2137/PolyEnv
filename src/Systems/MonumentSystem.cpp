//
// Created by Fryderyk Niedzwiecki on 03/02/2026.
//

#include "MonumentSystem.h"

#include <cstddef>

#include "Game.h"
#include "Player/Player.h"
#include "World/Settlements/City.h"
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
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;

    Player& pl = game.getPlayer(pid);
    (void)pl.addMonument(BuildingTypeEnum::GrandBazaar);
}

void MonumentSystem::onCityReachedLevel5(Game& game, Pos pos) {
    if (!game.getMap().inBounds(pos)) return;

    const Tile& t = game.getMap().at(pos);
    const CityId cid = t.getTerritoryCityId();
    if (cid == kNoCity) return;

    City* c = game.getCity(cid);
    if (!c) return;

    // ✅ Safety: only award when the city is actually level 5+
    if (c->getLevel() < 5) return;

    const PlayerId pid = static_cast<PlayerId>(c->getOwnerId());
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;

    Player& pl = game.getPlayer(pid);
    (void)pl.addMonument(BuildingTypeEnum::ParkOfFortune);
}


void MonumentSystem::onLighthouseCountUpdated(Game& game, PlayerId pid, int lighthouseCount) {
    if (lighthouseCount < 4) return;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;

    Player& pl = game.getPlayer(pid);
    (void)pl.addMonument(BuildingTypeEnum::EyeOfGod);
}

void MonumentSystem::onStarsUpdated(Game& game, PlayerId pid) {
    Player& pl = game.getPlayer(pid);

    if (pl.getStars() < 100) return;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;


    // Emperor's Tomb requires Trade tech
    if (!pl.hasTech(TechId::Trade)) return;

    (void)pl.addMonument(BuildingTypeEnum::EmperorsTomb);
}

void MonumentSystem::onKillsUpdated(Game& game, PlayerId pid, int totalKills) {
    if (totalKills < 10) return;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;

    Player& pl = game.getPlayer(pid);
    (void)pl.addMonument(BuildingTypeEnum::GateOfPower);
}

void MonumentSystem::onNoAttackTurnsUpdated(Game& game, PlayerId pid, int noAttackTurns) {
    if (noAttackTurns < 5) return;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;

    Player& pl = game.getPlayer(pid);

    // Polytopia: Pacifist task is unlocked by Meditation tech.
    if (!pl.hasTech(TechId::Meditation)) return;

    (void)pl.addMonument(BuildingTypeEnum::AltarOfPeace);
}

void MonumentSystem::onAllTechUnlockedUpdated(Game& game, PlayerId pid, bool allTechUnlocked) {
    if (!allTechUnlocked) return;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return;

    Player& pl = game.getPlayer(pid);
    (void)pl.addMonument(BuildingTypeEnum::TowerOfWisdom);
}


bool MonumentSystem::checkCityLevelForMonument(Game& game, City* c) {
    if (!c) return false;

    if (c->getLevel()>=5) return true;
    return false;

}

