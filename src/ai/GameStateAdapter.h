#ifndef GAME_ENGINE_GAMESTATEADAPTER_H
#define GAME_ENGINE_GAMESTATEADAPTER_H

#include <optional>
#include <vector>

#include "ActionSpace.h"
#include "game/Game.h"

// Adapter around Game that exposes a stable action/state API.
class GameStateAdapter final {
public:
    explicit GameStateAdapter(Game game);

    PlayerId currentPlayer() const;
    bool isTerminal() const;

    std::vector<Action> legalActions(PlayerId pid) const;
    void apply(const Action& a);
    std::vector<size_t> legalActionIds(PlayerId pid) const;
    // Hot-path variant used by batched C++ callers. The reference stays valid
    // until the next state mutation on this adapter.
    const std::vector<size_t>& legalActionIdsRef(PlayerId pid) const;
    std::vector<uint8_t> legalActionMask(PlayerId pid) const;
    std::optional<Action> decodeActionId(PlayerId pid, size_t actionId) const;
    std::optional<size_t> encodeActionId(const Action& action) const;
    const ActionSpace& actionSpace() const { return actionSpace_; }

    const Game& getGame() const { return game_; }
    Game& getGame() { return game_; }

private:
    void invalidateLegalCache();
    void ensureLegalCache(PlayerId pid) const;
    bool applyAction(const Action& a);

    Game game_;
    ActionSpace actionSpace_;
    mutable bool legalCacheValid_ = false;
    mutable PlayerId legalCachePid_ = kNoPlayer;
    mutable std::vector<size_t> legalIdsCache_;
    mutable std::vector<uint8_t> legalMaskCache_;
};

#endif // GAME_ENGINE_GAMESTATEADAPTER_H
