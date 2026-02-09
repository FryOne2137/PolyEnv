//
// Created by Fryderyk Niedzwiecki on 03/02/2026.
//

#ifndef GAME_ENGINE_LIGHTHOUSESYSTEM_H
#define GAME_ENGINE_LIGHTHOUSESYSTEM_H

#include <cstdint>
#include <vector>

#include "core/Ids.h"   // PlayerId
#include "world/Pos.h"  // Pos

class Game;

/**
 * LighthouseSystem
 *
 * Globalny system śledzący odkrywanie latarni morskich (4 rogi mapy).
 *
 * - Latarnie są obiektami globalnymi (świat).
 * - Odkrycie jest per-gracz.
 * - Każdy gracz może odkryć każdą latarnię dokładnie raz.
 * - System NIE zarządza fogiem – tylko reaguje na event „tile odkryty”.
 */
enum class LighthouseCorner : uint8_t {
    TopLeft = 0,
    BottomLeft = 1,
    TopRight = 2,
    BottomRight = 3,
};


class LighthouseSystem {
public:
    static bool isLighthousePos(const Game& game, Pos p);
    static uint8_t lighthouseIndex(const Game& game, Pos p);
    static bool onTileRevealed(Game& game, PlayerId pid, Pos p);
    static uint16_t getDiscoveredByMask(const Game& game, uint8_t lighthouseIdx);
    static bool hasPlayerDiscovered(const Game& game, uint8_t lighthouseIdx, PlayerId pid);

    // Returns list of players who have revealed this lighthouse corner
    static std::vector<PlayerId> getDiscoveredPlayers(const Game& game, LighthouseCorner corner);

    // Checks directly using LighthouseCorner enum
    static bool hasPlayerDiscovered(const Game& game, PlayerId pid,LighthouseCorner corner);
};

#endif // GAME_ENGINE_LIGHTHOUSESYSTEM_H