#include "runtime/GameSession.h"

#include <utility>

#include "content/skills/UnitSkill.h"

namespace {
using namespace polyenv_events;

ObservedEventType eventTypeFor(const Action& action) {
    switch (action.type) {
        case Action::Type::Move: return ObservedEventType::Move;
        case Action::Type::SpawnUnit: return ObservedEventType::SpawnUnit;
        case Action::Type::Attack: return ObservedEventType::Attack;
        case Action::Type::Heal: return ObservedEventType::Heal;
        case Action::Type::Build: return ObservedEventType::Build;
        case Action::Type::TileAction:
            if (action.tileAction == Action::TileActionKind::Fishing) return ObservedEventType::Fishing;
            if (action.tileAction == Action::TileActionKind::BuildRoad) return ObservedEventType::BuildRoad;
            // City ownership is retained in the observed map state, rather
            // than duplicated as an event history entry.
            if (action.tileAction == Action::TileActionKind::CaptureCity ||
                action.tileAction == Action::TileActionKind::FoundCity) return ObservedEventType::TileChanged;
            return ObservedEventType::TileChanged;
        case Action::Type::UnitUpgrade:
            return action.unitUpgrade == Action::UnitUpgradeKind::Disband
                ? ObservedEventType::UnitRemoved : ObservedEventType::UnitUpgraded;
        default: return ObservedEventType::TileChanged;
    }
}

bool shouldRecord(const Action& action) {
    switch (action.type) {
        case Action::Type::Move:
        case Action::Type::Attack:
        case Action::Type::Heal:
        case Action::Type::SpawnUnit:
        case Action::Type::Build:
            return true;
        case Action::Type::TileAction:
            return action.tileAction != Action::TileActionKind::CaptureCity &&
                   action.tileAction != Action::TileActionKind::FoundCity &&
                   action.tileAction != Action::TileActionKind::None;
        case Action::Type::UnitUpgrade:
            return action.unitUpgrade != Action::UnitUpgradeKind::None;
        default:
            return false; // EndTurn, BuyTech and city rewards are state, not world events.
    }
}

bool visibleTo(const Game& game, PlayerId player, int tileIndex) {
    const Map& map = game.getMap();
    const int width = map.getWidth();
    if (tileIndex < 0 || width <= 0) return false;
    const Pos pos{tileIndex % width, tileIndex / width};
    return map.inBounds(pos) && (static_cast<uint16_t>(map.at(pos).getVisibility()) & (uint16_t(1) << player));
}

int32_t observedUnitId(ObservationKnowledge& knowledge, size_t observer, UnitId unit) {
    if (unit == kNoUnit) return -1;
    if (observer >= knowledge.observedUnitIdsByPlayer.size()) return -1;
    auto& ids = knowledge.observedUnitIdsByPlayer[observer];
    const auto found = ids.find(unit);
    if (found != ids.end()) return found->second;
    const int32_t id = knowledge.nextObservedUnitIdByPlayer[observer]++;
    ids.emplace(unit, id);
    return id;
}

struct CombatAffectedUnit {
    UnitId unitId = kNoUnit;
    int tileIndex = -1;
    int unitType = -1;
    int hpBefore = -1;
    int hpAfter = -1;
    bool splash = false;
};

// Emits precisely one event per observer.  This is deliberately O(players),
// never O(players * map tiles): event details use only source/target visibility.
void appendActionEvent(ObservedEventJournal& journal, ObservationKnowledge& knowledge, const Game& after,
                       const Action& action, int sourceIndex, int targetIndex,
                       const std::vector<uint8_t>& sourceVisibleBefore,
                       const std::vector<uint8_t>& targetVisibleBefore,
                       int attackerHpBefore, int attackerHpAfter, int targetHpBefore, int targetHpAfter,
                       UnitId sourceUnitId, UnitId targetUnitId,
                       int sourceUnitType, int targetUnitType,
                       int sourceHpBefore, int sourceHpAfter, int targetHpBeforeAll, int targetHpAfterAll,
                       const std::vector<CombatAffectedUnit>& affectedUnits,
                       uint64_t actionSequence, int round) {
    if (!shouldRecord(action)) return;
    for (size_t player = 0; player < after.getPlayers().size(); ++player) {
        const PlayerId observer = static_cast<PlayerId>(player);
        const bool ownsAction = observer == action.pid;
        const bool sourceVisible = (player < sourceVisibleBefore.size() && sourceVisibleBefore[player]) ||
            visibleTo(after, observer, sourceIndex);
        const bool targetVisible = (player < targetVisibleBefore.size() && targetVisibleBefore[player]) ||
            visibleTo(after, observer, targetIndex);
        if (!ownsAction && !sourceVisible && !targetVisible) continue;
        ObservedEventRecord event;
        event.actionSequence = actionSequence;
        event.round = round;
        event.turn = after.getTurnNumber();
        event.typeId = static_cast<int16_t>(eventTypeFor(action));
        if (action.type == Action::Type::TileAction)
            event.tileActionKind = static_cast<int16_t>(action.tileAction);
        if (action.type == Action::Type::Build)
            event.buildingType = static_cast<int16_t>(action.building);
        if (action.type == Action::Type::SpawnUnit)
            event.spawnType = static_cast<int16_t>(action.spawnType);
        if (action.type == Action::Type::UnitUpgrade) {
            event.unitUpgradeKind = static_cast<int16_t>(action.unitUpgrade);
            if (const Unit* upgraded = after.getUnit(sourceUnitId))
                event.upgradedUnitType = static_cast<int16_t>(upgraded->getType());
        }
        // Turns are sequential and public: at this point only action.pid had
        // the right to act.  Therefore an observed action identifies its
        // player and tribe even if the acting unit stayed in fog.  It does not
        // reveal the unit's hidden source position or type.
        event.actorPlayer = static_cast<int16_t>(action.pid);
        event.actorTribe = static_cast<int16_t>(after.getPlayer(action.pid).getTribeType());
        if (targetVisible || ownsAction) {
            event.targetIndex = targetIndex;
            event.flags |= TargetVisible;
        }
        if (sourceVisible || ownsAction) {
            event.sourceIndex = sourceIndex;
            event.flags |= SourceVisible;
        }
        const bool sourceUnitVisible = sourceVisible || ownsAction ||
            (action.type == Action::Type::Move && targetVisible);
        if (sourceUnitVisible) {
            event.sourceUnitType = static_cast<int16_t>(sourceUnitType);
            event.sourceObservedUnitId = observedUnitId(knowledge, player, sourceUnitId);
            event.sourceUnitHpBefore = static_cast<int16_t>(sourceHpBefore);
            event.sourceUnitHpAfter = static_cast<int16_t>(sourceHpAfter);
        }
        if (targetVisible || ownsAction) {
            event.targetUnitType = static_cast<int16_t>(targetUnitType);
            event.targetObservedUnitId = observedUnitId(knowledge, player, targetUnitId);
            event.targetUnitHpBefore = static_cast<int16_t>(targetHpBeforeAll);
            event.targetUnitHpAfter = static_cast<int16_t>(targetHpAfterAll);
        }
        if (!sourceVisible && !ownsAction && sourceIndex >= 0) {
            event.flags |= UnknownSource;
        }
        if (action.type == Action::Type::Attack) {
            if (targetVisible || ownsAction) {
                event.hpBefore = static_cast<int16_t>(targetHpBefore);
                event.hpAfter = static_cast<int16_t>(targetHpAfter);
                event.damage = targetHpBefore >= 0 && targetHpAfter >= 0
                ? static_cast<int16_t>(targetHpBefore - targetHpAfter) : -1;
            }
            // Death is an observable fact only for the visible/own unit.  A
            // retaliating defender may kill the attacker, so source and target
            // must be represented independently.
            if (targetVisible || ownsAction)
                event.unitDestroyed = targetHpBefore >= 0 && targetHpAfter == 0;
            if (sourceVisible || ownsAction)
                event.sourceUnitDestroyed = attackerHpBefore >= 0 && attackerHpAfter == 0;
            if (sourceVisible || ownsAction || (action.type == Action::Type::Move && targetVisible)) {
                event.attackerHpBefore = static_cast<int16_t>(attackerHpBefore);
                event.attackerHpAfter = static_cast<int16_t>(attackerHpAfter);
            }
            for (const CombatAffectedUnit& affected : affectedUnits) {
                const bool affectedVisible = visibleTo(after, observer, affected.tileIndex) ||
                    (affected.tileIndex == targetIndex && player < targetVisibleBefore.size() && targetVisibleBefore[player]);
                if (!ownsAction && !affectedVisible) continue;
                ObservedEventRecord::AffectedUnit item;
                item.observedUnitId = observedUnitId(knowledge, player, affected.unitId);
                item.tileIndex = affected.tileIndex;
                item.unitType = static_cast<int16_t>(affected.unitType);
                item.hpBefore = static_cast<int16_t>(affected.hpBefore);
                item.hpAfter = static_cast<int16_t>(affected.hpAfter);
                item.damage = affected.hpBefore >= 0 && affected.hpAfter >= 0
                    ? static_cast<int16_t>(affected.hpBefore - affected.hpAfter) : -1;
                item.destroyed = affected.hpBefore >= 0 && affected.hpAfter == 0;
                item.splash = affected.splash;
                event.affectedUnits.push_back(item);
            }
        }
        journal.append(player, event);
    }
}
} // namespace

GameSession::GameSession(Game game)
    : state(std::move(game)) {
    events.reset(this->game().getPlayers().size());
    observations.observedUnitIdsByPlayer.resize(this->game().getPlayers().size());
    observations.nextObservedUnitIdByPlayer.assign(this->game().getPlayers().size(), 0);
}

bool GameSession::apply(const Action& action, std::optional<size_t> actionId) {
    const int round = static_cast<int>(game().getTurnNumber());
    const uint64_t actionSequence = nextActionSequence_++;
    int sourceIndex = -1;
    const int width = game().getMap().getWidth();
    if (action.unit != kNoUnit) {
        if (const Unit* unit = game().getUnit(action.unit)) sourceIndex = unit->getPos().y * width + unit->getPos().x;
    }
    const Pos targetPos = (action.type == Action::Type::Move || action.type == Action::Type::Attack)
        ? action.target : action.pos;
    const int targetIndex = game().getMap().inBounds(targetPos)
        ? targetPos.y * width + targetPos.x : -1;
    // A spawn has no unit source.  Its city/tile is both the origin and the
    // destination from the observer's perspective.
    if (action.type == Action::Type::SpawnUnit) sourceIndex = targetIndex;
    int attackerHpBefore = -1;
    int targetHpBefore = -1;
    int sourceUnitType = -1;
    int targetUnitType = -1;
    int sourceHpBefore = -1;
    int sourceHpAfter = -1;
    int targetHpAfterAll = -1;
    UnitId sourceUnitId = action.unit;
    UnitId targetUnit = kNoUnit;
    std::vector<CombatAffectedUnit> affectedUnits;
    if (const Unit* sourceUnit = game().getUnit(action.unit)) {
        sourceUnitType = static_cast<int>(sourceUnit->getType());
        sourceHpBefore = sourceUnit->getHealth();
    }
    if (action.type == Action::Type::SpawnUnit) targetUnitType = static_cast<int>(action.spawnType);
    if (action.type == Action::Type::Attack) {
        bool hasSplash = false;
        if (const Unit* attacker = game().getUnit(action.unit)) {
            attackerHpBefore = attacker->getHealth();
            hasSplash = attacker->hasSkill(UnitSkill::Splash);
        }
        targetUnit = game().getMap().unitOn(targetPos);
        if (const Unit* target = game().getUnit(targetUnit)) {
            targetHpBefore = target->getHealth();
            targetUnitType = static_cast<int>(target->getType());
        }
        // Primary target plus the eight possible splash positions.  This is a
        // fixed-size local probe, not a map snapshot.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const Pos pos{targetPos.x + dx, targetPos.y + dy};
                if (!game().getMap().inBounds(pos)) continue;
                const UnitId unitId = game().getMap().unitOn(pos);
                const Unit* unit = game().getUnit(unitId);
                if (unitId == kNoUnit || !unit) continue;
                const bool isPrimary = dx == 0 && dy == 0;
                if (!isPrimary && (!hasSplash || unit->getOwnerId() == action.pid)) continue;
                affectedUnits.push_back({unitId, pos.y * width + pos.x,
                    static_cast<int>(unit->getType()), unit->getHealth(), -1,
                    !isPrimary});
            }
        }
    }
    std::vector<uint8_t> sourceVisibleBefore(game().getPlayers().size());
    std::vector<uint8_t> targetVisibleBefore(game().getPlayers().size());
    for (size_t player = 0; player < game().getPlayers().size(); ++player) {
        const PlayerId observer = static_cast<PlayerId>(player);
        sourceVisibleBefore[player] = visibleTo(game(), observer, sourceIndex);
        targetVisibleBefore[player] = visibleTo(game(), observer, targetIndex);
    }
    state.apply(action);
    if (actionId) replay.record(*actionId);
    int attackerHpAfter = -1;
    int targetHpAfter = -1;
    if (action.type == Action::Type::Attack) {
        if (const Unit* attacker = game().getUnit(action.unit)) attackerHpAfter = attacker->getHealth();
        if (const Unit* target = game().getUnit(targetUnit)) targetHpAfter = target->getHealth();
        if (targetUnit != kNoUnit && targetHpAfter < 0) targetHpAfter = 0;
    }
    for (CombatAffectedUnit& affected : affectedUnits) {
        if (const Unit* unit = game().getUnit(affected.unitId)) affected.hpAfter = unit->getHealth();
        else affected.hpAfter = 0;
    }
    if (const Unit* sourceUnit = game().getUnit(sourceUnitId)) sourceHpAfter = sourceUnit->getHealth();
    if (action.type == Action::Type::SpawnUnit) {
        targetUnit = game().getMap().unitOn(targetPos);
        if (const Unit* spawned = game().getUnit(targetUnit)) {
            targetUnitType = static_cast<int>(spawned->getType());
            targetHpBefore = 0;
            targetHpAfterAll = spawned->getHealth();
        }
    } else {
        targetHpAfterAll = targetHpAfter;
    }
    appendActionEvent(events, observations, game(), action, sourceIndex, targetIndex,
                      sourceVisibleBefore, targetVisibleBefore, attackerHpBefore, attackerHpAfter,
                      targetHpBefore, targetHpAfter, sourceUnitId, targetUnit, sourceUnitType, targetUnitType,
                      sourceHpBefore, sourceHpAfter, targetHpBefore, targetHpAfterAll, affectedUnits,
                      actionSequence, round);
    return true;
}

std::shared_ptr<GameSession> GameSession::clone() const {
    return std::make_shared<GameSession>(*this);
}

std::shared_ptr<GameSession> GameSession::cloneForSearch() const {
    // Do not start from the default copy constructor: replay and per-observer
    // event vectors can grow with a real match, while native MCTS never reads
    // them. Copy only the authoritative Game plus fog-of-war knowledge.
    Game gameCopy = state.getGame();
    auto result = std::make_shared<GameSession>(std::move(gameCopy));
    result->observations = observations;
    result->events.reset(result->game().getPlayers().size());
    result->observations.lastRevealedByPlayer.assign(result->game().getPlayers().size(), {});
    return result;
}

const polyenv_events::PlayerEventJournal* GameSession::eventsFor(PlayerId player) const {
    if (player == kNoPlayer) return nullptr;
    return events.stream(static_cast<size_t>(player));
}
