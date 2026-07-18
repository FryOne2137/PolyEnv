#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/Ids.h"

class Game;

// Builds isolated rollout worlds from a player's observation plus a completed
// token-map hypothesis. This is intentionally outside Game: Game itself
// remains the authoritative rules/state container and has no prediction API.
//
// The rollout receives a deterministic synthetic world seed derived from
// public position metadata and the supplied hypothesis. It never inherits the
// authoritative world's seed, because that seed controls hidden random
// outcomes in the live game.
class BeliefWorldBuilder final {
public:
    static Game build(const Game& observedSource,
                      PlayerId perspective,
                      const std::vector<std::vector<int>>& completedTokens);

    // Hot-path variant for batched callers. `tokens` is a row-major,
    // contiguous [rowCount, columnCount] matrix. It performs the same
    // validation and isolation as build(), without materialising a nested
    // vector for every belief world.
    static Game buildFlat(const Game& observedSource,
                          PlayerId perspective,
                          const int32_t* tokens,
                          size_t rowCount,
                          size_t columnCount);
};
