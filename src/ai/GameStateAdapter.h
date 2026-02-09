#ifndef GAME_ENGINE_GAMESTATEADAPTER_H
#define GAME_ENGINE_GAMESTATEADAPTER_H

#include <optional>
#include <vector>

#include "IGameState.h"
#include "ActionSpace.h"
#include "game/Game.h"

// Adapter around Game that exposes IGameState for tree-search algorithms.
// It keeps rules delegated to existing game systems and stores full-state
// snapshots for robust undo semantics.
class GameStateAdapter final : public IGameState {
public:
    explicit GameStateAdapter(Game game);

    PlayerId currentPlayer() const override;
    bool isTerminal() const override;
    float evaluate(PlayerId forPlayer) const override;

    std::vector<Action> legalActions(PlayerId pid) const override;
    void apply(const Action& a) override;
    void undo(const Action& a) override;
    std::unique_ptr<IGameState> clone() const override;

    uint64_t hash() const override;
    std::vector<uint8_t> legalActionMask(PlayerId pid) const;
    std::optional<Action> decodeActionId(PlayerId pid, size_t actionId) const;
    std::optional<size_t> encodeActionId(const Action& action) const;
    const ActionSpace& actionSpace() const { return actionSpace_; }

    const Game& getGame() const { return game_; }
    Game& getGame() { return game_; }

private:
    static uint64_t fnv1a64(uint64_t h, uint64_t v);
    bool applyAction(const Action& a);

    Game game_;
    std::vector<Game> undoStack_;
    ActionSpace actionSpace_;
};

#endif // GAME_ENGINE_GAMESTATEADAPTER_H
