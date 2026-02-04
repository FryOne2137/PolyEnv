// ScoreSystem.h
//
// Created by Fryderyk Niedzwiecki on 04/02/2026.
//

#ifndef GAME_ENGINE_SCORESYSTEM_H
#define GAME_ENGINE_SCORESYSTEM_H

#include "tribes/Tribe.h"
#include "Player/Player.h"

class Game;

class ScoreSystem {
public:
    // ===== MAIN API =====
    // Recomputes total score for given player based on current game state
    static int getScore(const Game &game, PlayerId playerId);

private:
    // ===== STARTING SCORE =====
    static int startingScore(TribeType tribe);

    // ===== EXPLORATION / MAP =====
    static int exploredTiles(int tiles);
    static int territoryTiles(int tiles);

    // ===== UNITS =====
    static bool isSuperUnit(const Unit& u);
    static int costStarsByUnitType(UnitType t);
    static int getBaseUnitCostStars(const Unit& u);

    static int unitCostStars(int stars);
    static int superUnits(int count);

    // ===== CITIES =====
    static int cityLevel(int level, int population);


    // ===== BUILDINGS =====
    static int parks(int count);
    static int monuments(int count);
    static bool isMonument(BuildingTypeEnum b);

    // ===== TEMPLES =====
    static int templeLevel(int level);

    // ===== TECHNOLOGIES =====
    static int techTier(int tier);

};

#endif // GAME_ENGINE_SCORESYSTEM_H