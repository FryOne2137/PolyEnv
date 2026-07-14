#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace polyenv_events {

enum class ObservedEventType : int16_t {
    TileRevealed = 0,
    TileChanged = 1,
    UnitDamaged = 2,
    UnitDestroyed = 3,
    StarsChanged = 4,
    TechsChanged = 5,
    GameOver = 6,
    Move = 7,
    SpawnUnit = 8,
    Attack = 9,
    Heal = 10,
    Fishing = 11,
    BuildRoad = 12,
    Build = 13,
    UnitRemoved = 14,
    UnitUpgraded = 15,
};

enum ObservedEventFlag : uint16_t {
    SourceVisible = 1 << 0,
    TargetVisible = 1 << 1,
    OwnTarget = 1 << 2,
    UnknownSource = 1 << 3,
};

struct ObservedEventRecord {
    uint64_t sequence = 0;
    // Shared session action id: equal for every observer's copy of one action.
    uint64_t actionSequence = 0;
    // Full round number before the action was applied.
    int32_t round = 0;
    int32_t turn = 0;
    int16_t typeId = static_cast<int16_t>(ObservedEventType::TileChanged);
    uint16_t flags = 0;
    int32_t sourceIndex = -1;
    int32_t targetIndex = -1;
    int16_t damage = -1;
    int16_t hpBefore = -1;
    int16_t hpAfter = -1;
    int16_t attackerHpBefore = -1;
    int16_t attackerHpAfter = -1;
    // Tribe is available only if the observer can identify the actor.
    int16_t actorTribe = -1;
    int16_t actorPlayer = -1;
    // Concrete tile mutation metadata.  These are Action enum values, or -1
    // when the event is not a tile action/build.
    int16_t tileActionKind = -1;
    int16_t buildingType = -1;
    int16_t spawnType = -1;
    int16_t unitUpgradeKind = -1;
    int16_t upgradedUnitType = -1;
    // Generic, observation-filtered unit descriptors for every event kind.
    int16_t sourceUnitType = -1;
    int16_t targetUnitType = -1;
    int32_t sourceObservedUnitId = -1;
    int32_t targetObservedUnitId = -1;
    int16_t sourceUnitHpBefore = -1;
    int16_t sourceUnitHpAfter = -1;
    int16_t targetUnitHpBefore = -1;
    int16_t targetUnitHpAfter = -1;
    bool unitDestroyed = false;
    bool sourceUnitDestroyed = false;
    struct AffectedUnit {
        int32_t observedUnitId = -1;
        int32_t tileIndex = -1;
        int16_t unitType = -1;
        int16_t damage = -1;
        int16_t hpBefore = -1;
        int16_t hpAfter = -1;
        bool destroyed = false;
        bool splash = false;
    };
    std::vector<AffectedUnit> affectedUnits;
};

struct PlayerEventJournal {
    uint64_t nextSequence = 0;
    std::vector<ObservedEventRecord> events;
};

// Owns all private streams. Callers must choose the perspective themselves;
// public bindings should expose only the stream belonging to their caller.
class ObservedEventJournal {
public:
    void reset(size_t playerCount) { journals_.assign(playerCount, {}); }

    void append(size_t player, ObservedEventRecord event) {
        if (player >= journals_.size()) return;
        PlayerEventJournal& journal = journals_[player];
        event.sequence = journal.nextSequence++;
        journal.events.push_back(event);
    }

    const PlayerEventJournal* stream(size_t player) const {
        return player < journals_.size() ? &journals_[player] : nullptr;
    }

    uint64_t nextSequence(size_t player) const {
        const PlayerEventJournal* journal = stream(player);
        return journal ? journal->nextSequence : 0;
    }

private:
    std::vector<PlayerEventJournal> journals_;
};

} // namespace polyenv_events
