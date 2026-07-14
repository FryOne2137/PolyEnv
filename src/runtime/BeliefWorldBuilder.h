#pragma once

#include <vector>

#include "core/Ids.h"

class Game;

// Builds isolated rollout worlds from a player's observation plus a completed
// token-map hypothesis.  This is intentionally outside Game: Game itself
// remains the authoritative rules/state container and has no prediction API.
class BeliefWorldBuilder final {
public:
    static Game build(const Game& observedSource,
                      PlayerId perspective,
                      const std::vector<std::vector<int>>& completedTokens);
};
