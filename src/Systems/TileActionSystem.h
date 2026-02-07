//
// Created by Fryderyk Niedzwiecki on 25/01/2026.
//

#ifndef GAME_ENGINE_TILEACTIONSYSTEM_H
#define GAME_ENGINE_TILEACTIONSYSTEM_H

#include "Core/Ids.h"
#include "World/Pos.h"

class Game;

// Actions that modify a tile (terrain/resources) and apply rewards/costs.
// Validation and game rules should live here (tech requirements, stars cost, territory ownership, etc.).
class TileActionSystem {
public:
    // --- Resource / terrain actions (Polytopia-style) ---
    static bool hunt(Game& game, PlayerId pid, Pos pos);

    static bool organization(Game &game, PlayerId pid, Pos pos);

    static bool fishing(Game& game, PlayerId pid, Pos pos);

    static bool clearForest(Game& game, PlayerId pid, Pos pos);
    static bool burnForest(Game& game, PlayerId pid, Pos pos);
    static bool growForest(Game& game, PlayerId pid, Pos pos);

    static bool destroy(Game& game, PlayerId pid, Pos pos);

    static bool destroyResource(Game& game, PlayerId pid, Pos pos) { return destroy(game, pid, pos); }
    static bool buildRoad(Game& game, PlayerId pid, Pos pos);
    static bool buildBridge(Game& game, PlayerId pid, Pos pos);
};

#endif //GAME_ENGINE_TILEACTIONSYSTEM_H