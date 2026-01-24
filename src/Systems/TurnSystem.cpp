//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "Systems/TurnSystem.h"

#include "Game.h"   // Game, Player, Unit access


#include <algorithm>


int TurnSystem::calcIncomeForPlayer(const Game& game, PlayerId pid) {
    const Player& p = game.getPlayer(pid);

    // Base income so stars still increase even if city ownership/lookup isn't fully wired yet.
    int income = 0;

    for (CityId cid : p.getCities()) {
        const City* c = game.getCity(cid);
        if (!c) continue;
        income += static_cast<int>(c->getStarsPerRound());
    }

    return std::max(0, income);
}


void TurnSystem::startTurn(Game& game) {

    if (game.getTurnNumber()!=0) {
        applyIncomeForCurrentPlayer(game);
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
}

void TurnSystem::refreshUnitsForCurrentPlayer(Game& game) {
    // --- adapt this if your Game exposes current player differently ---
    const PlayerId pid = game.getCurrentPlayerId();

    Player& p = game.getPlayer(pid);              // <-- jeśli nie masz, użyj game.players[pid]

    const auto& unitIds = p.getUnits();
    for (UnitId uid : unitIds) {
        Unit* u = game.getUnit(uid);
        if (!u) continue;

        // reset per-turn flags
        u->setMovedThisTurn(false);
        u->setAttackedThisTurn(false);

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
        // refreshed = UnitFactory::getDefaultMove(u->getType());
        //
        // Na dziś: spróbuj odtworzyć do bieżącej wartości, jeśli jest >0,
        // ale po ruchu mogło być 0, więc ustaw minimum 1.
        refreshed = std::max(1, u->getMovePoints());
        u->setMovePoints(refreshed);
    }
}

void TurnSystem::applyIncomeForCurrentPlayer(Game& game) {
    const PlayerId pid = game.getCurrentPlayerId();
    Player& p = game.getPlayer(pid);

    const int income = TurnSystem::calcIncomeForPlayer(game, pid);
    if (income > 0) {
        p.addStars(income);
    }
}