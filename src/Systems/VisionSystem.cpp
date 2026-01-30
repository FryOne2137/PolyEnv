//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "VisionSystem.h"

#include "Game.h"
#include "World/Map.h"
#include "World/Tile.h"
#include "terrain/BaseTerrainEnum.h"
#include "terrain/VisibilityEnum.h"

#include <algorithm>
#include <cstdint>
#include <vector>

void VisionSystem::revealFromUnit(Game& game, UnitId uid) {
    Unit* u = game.getUnit(uid);
    if (!u) return;

    const PlayerId pid = u->getOwnerId();
    Pos p = u->getPos();

    int r = u->getVisionRange();
    // int r = 2;
    const Tile& t = game.getMap().at(p);
    if (t.getBaseTerrain() == BaseTerrainEnum::Mountain) {
        r = std::max(r, 2);
    }
    revealArea(game, pid, p, r);
}


void VisionSystem::revealForPlayerFromUnits(Game& game, PlayerId playerId) {
    // Dodajemy widoczność na bazie wszystkich jednostek gracza.
    // UWAGA: To nie "czyści" fog-of-war (czyli nie usuwa widoczności).
    const Player& p = game.getPlayer(playerId);

    for (UnitId uid : p.getUnits()) {
        // Use revealFromUnit so mountain bonus (range 2) is applied consistently.
        revealFromUnit(game, uid);
    }
}

void VisionSystem::revealArea(Game& game, PlayerId pid, Pos center, int range) {
    Map& map = game.getMap();

    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            Pos p{center.x + dx, center.y + dy};
            if (!map.inBounds(p)) continue;

            Tile& t = map.at(p);
            VisibilityEnum v = t.getVisibility();
            reveal(v, static_cast<PlayerIndex>(pid));
            t.setVisibility(v);
        }
    }
}