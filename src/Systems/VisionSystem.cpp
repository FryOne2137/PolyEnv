//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "VisionSystem.h"

#include "Game.h"
#include "World/Map.h"
#include "World/Tile.h"

#include <algorithm>

VisibilityEnum VisionSystem::bitForPlayer(PlayerId playerId) {
    // max 16 players => bits 0..15
    if (playerId >= 16) return VisibilityEnum::None;
    return static_cast<VisibilityEnum>(static_cast<uint16_t>(1u) << playerId);
}

void VisionSystem::revealFromUnit(Game& game, UnitId unitId) {
    Unit* u = game.getUnit(unitId);
    if (!u) return;

    const PlayerId owner = u->getOwnerId();
    const Pos center = u->getPos();
    const int range = u->getVisionRange();

    revealArea(game, owner, center, range);
}

void VisionSystem::revealForPlayerFromUnits(Game& game, PlayerId playerId) {
    // Dodajemy widoczność na bazie wszystkich jednostek gracza.
    // UWAGA: To nie "czyści" fog-of-war (czyli nie usuwa widoczności).
    const Player& p = game.getPlayer(playerId);

    for (UnitId uid : p.getUnits()) {
        const Unit* u = game.getUnit(uid);
        if (!u) continue;

        revealArea(game, playerId, u->getPos(), u->getVisionRange());
    }
}

void VisionSystem::revealArea(Game& game, PlayerId playerId, Pos center, int range) {
    if (range < 0) return;

    Map& map = game.getMap();
    if (!map.inBounds(center)) return;

    const VisibilityEnum bit = bitForPlayer(playerId);

    // Square / Chebyshev disk: max(|dx|, |dy|) <= range
    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) > range) continue;

            Pos p{ center.x + dx, center.y + dy };
            if (!map.inBounds(p)) continue;

            Tile& t = map.at(p);

            // WYMAGA: Tile musi mieć getVisibility()/setVisibility()
            // Jeśli nie masz jeszcze, dopisz proste get/set w Tile.
            VisibilityEnum v = t.getVisibility();
            v |= bit;
            t.setVisibility(v);
        }
    }
}