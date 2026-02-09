//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "systems/TurnSystem.h"

#include "../game/Game.h"
#include "systems/CitySystem.h"

#include "systems/PlayerSystem.h"
#include "world/Map.h"
#include "world/Tile.h"
#include "terrain/BuildingTypeEnum.h"
#include <algorithm>
#include "systems/MonumentSystem.h"
#include "systems/UnitSystem.h"
#include "systems/StarsSystem.h"

int TurnSystem::calcIncomeForPlayer(const Game& game, PlayerId pid) {
    return StarsSystem::calcIncomeForPlayer(game, pid);
}


void TurnSystem::startTurn(Game& game) {
    const PlayerId pid = game.getCurrentPlayerId();


    if (game.getTurnNumber()!=0) {
        applyIncomeForCurrentPlayer(game,pid);
        MonumentSystem::onStarsUpdated(game,pid);

    }

    // TODO: start-of-turn effects:
    // - heal in cities
    // - poison ticks
    // - reset "levelUpRewardPending" etc.
}

void TurnSystem::endTurn(Game& game) {
    refreshUnitsForCurrentPlayer(game);

    // TODO: end-of-turn effects:
    // - resolve "delayed" effects
    // - cleanup flags if needed

    // Pacifist task (Altar of Peace): count consecutive turns with no attacks.
    {
        const PlayerId pid = game.getCurrentPlayerId();

        if (PlayerSystem::getAttackedThisTurn(game, pid)) {
            PlayerSystem::resetNoAttackTurns(game, pid);
        } else {
            PlayerSystem::incrementNoAttackTurns(game, pid);
        }

        // Clear per-turn attack flag for the next turn.
        PlayerSystem::setAttackedThisTurn(game, pid, false);

        // Notify monuments.
        MonumentSystem::onNoAttackTurnsUpdated(
            game,
            pid,
            static_cast<int>(PlayerSystem::getNoAttackTurns(game, pid))
        );
    }
}

void TurnSystem::refreshUnitsForCurrentPlayer(Game& game) {
    // --- adapt this if your Game exposes current player differently ---
    const PlayerId pid = game.getCurrentPlayerId();

    const auto& unitIds = PlayerSystem::getUnits(game, pid);
    for (UnitId uid : unitIds) {
        if (!UnitSystem::unitExists(game, uid)) continue;

        // reset per-turn flags
        UnitSystem::setMovedThisTurn(game, uid, false);
        UnitSystem::setAttackedThisTurn(game, uid, false);

        // refresh movement points
        // Jeśli trzymasz tylko "movePoints" jako max i potem odejmujesz w ruchu:
        // to ustawiasz tu na max.
        //
        // Uwaga: najlepiej mieć w Unit: maxMovePoints + movePointsRemaining,
        // ale skoro masz tylko movePoints, to traktuj to jako "pozostały" i tu odnawiaj.
        //
        // Jeśli nie masz maxa osobno, przyjmij że "movePoints" w Unit to maks,
        // a w systemie ruchu przechowujesz "remaining" gdzie indziej.
        //
        // Najprostsza wersja (działa z Twoim obecnym Unit):
        // - ruch system odejmuje z movePoints
        // - tu odtwarzasz do wartości domyślnej zależnej od typu
        // (na razie: 1)
        int refreshed = 1;

        // Jeżeli masz już UnitFactory/definicje, podmień:
        // refreshed = UnitFactory::getDefaultMove(UnitSystem::getType(game, uid));
        //
        // Na dziś: spróbuj odtworzyć do bieżącej wartości, jeśli jest >0,
        // ale po ruchu mogło być 0, więc ustaw minimum 1.
        refreshed = std::max(1, UnitSystem::getMovePoints(game, uid));
        UnitSystem::setMovePoints(game, uid, refreshed);
    }
}

void TurnSystem::applyIncomeForCurrentPlayer(Game& game, PlayerId pid) {
    StarsSystem::applyIncomeForPlayer(game, pid);
}