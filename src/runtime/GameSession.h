#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ai/GameStateAdapter.h"
#include "python/ObservedEventJournal.h"
#include "replay/ReplayRecorder.h"

using polyenv_events::ObservedEventJournal;

// The knowledge which may be exposed to each player.  This deliberately lives
// next to the event journal: rendering, Python and search must make decisions
// from the same per-player information boundary.
struct ObservationKnowledge {
    std::vector<std::vector<uint8_t>> knownByPlayer;
    std::vector<std::vector<int>> lastRevealedByPlayer;
    std::vector<std::unordered_map<UnitId, int32_t>> observedUnitIdsByPlayer;
    std::vector<int32_t> nextObservedUnitIdByPlayer;
};

// One authoritative runtime state.  UI and bindings are adapters around this
// class; neither owns a second Game, visibility mask, replay nor event stream.
class GameSession {
public:
    explicit GameSession(Game game);

    const Game& game() const { return state.getGame(); }
    Game& game() { return state.getGame(); }
    PlayerId currentPlayer() const { return state.currentPlayer(); }
    bool apply(const Action& action, std::optional<size_t> actionId = std::nullopt);
    std::shared_ptr<GameSession> clone() const;

    const polyenv_events::PlayerEventJournal* eventsFor(PlayerId player) const;
    const polyenv_events::PlayerEventJournal* eventsForCurrentPlayer() const {
        return eventsFor(currentPlayer());
    }

    GameStateAdapter state;
    ReplayRecorder replay;
    ObservationKnowledge observations;
    ObservedEventJournal events;

private:
    uint64_t nextActionSequence_ = 0;
};
