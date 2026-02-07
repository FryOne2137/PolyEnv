//
// Created by Fryderyk Niedzwiecki on 03/02/2026.
//

#ifndef GAME_ENGINE_MONUMENTSYSTEM_H
#define GAME_ENGINE_MONUMENTSYSTEM_H

#include <cstddef>

#include "Core/Ids.h"
#include "Pos.h"

class Game;
class City;

// Central system responsible for awarding monuments.
// This system is stateless and should be called by other systems
// when relevant game state transitions occur.
class MonumentSystem {
public:
    // Grand Bazaar: connect 5 cities to the capital
    static void onConnectedCitiesUpdated(Game& game, PlayerId pid, size_t connectedNonCapitalCities);

    // Park of Fortune: a city reaches level 5
    static void onCityReachedLevel5(Game& game, Pos pos);

    // Eye of God: player controls 4 lighthouses
    static void onLighthouseCountUpdated(Game& game, PlayerId pid, int lighthouseCount);

    // Emperor's Tomb: reach 100 stars
    static void onStarsUpdated(Game &game, PlayerId pid);

    // Gate of Power: reach 10 kills
    static void onKillsUpdated(Game &game, PlayerId pid, int totalKills);

    // Altar of Peace: 5 turns without attacking
    static void onNoAttackTurnsUpdated(Game& game, PlayerId pid, int noAttackTurns);

    // Tower of Wisdom: unlock all technologies
    static void onAllTechUnlockedUpdated(Game& game, PlayerId pid, bool allTechUnlocked);

private:
    static bool checkCityLevelForMonument(Game& game, CityId cid);
};

#endif // GAME_ENGINE_MONUMENTSYSTEM_H