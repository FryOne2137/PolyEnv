//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_VISIONSYSTEM_H
#define GAME_ENGINE_VISIONSYSTEM_H

#include <cstdint>

#include "units/Unit.h"      // UnitId, PlayerId
#include "World/Pos.h"       // Pos
#include "terrain/VisibilityEnum.h"

class Game;

// Fog-of-war / visibility updates.
// Each unit reveals tiles within its vision range (in tiles, not pixels).
class VisionSystem {
public:
    // Reveal tiles around a single unit using Unit::getVisionRange().
    static void revealFromUnit(Game& game, UnitId unitId);

    // Recompute (union) visibility for a player from all their units.
    // NOTE: This function only ADDS visibility (like Polytopia fog). It does not hide tiles again.
    static void revealForPlayerFromUnits(Game& game, PlayerId playerId);

    // Reveal a square/chebyshev disk centered at `center` with radius `range`.
    static void revealArea(Game& game, PlayerId playerId, Pos center, int range);

    // Helper: convert playerId (0..15) into a VisibilityEnum bit.
    static VisibilityEnum bitForPlayer(PlayerId playerId);
};

#endif //GAME_ENGINE_VISIONSYSTEM_H