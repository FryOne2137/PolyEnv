//
// Created by Fryderyk Niedzwiecki on 09/02/2026.
//

#ifndef GAME_ENGINE_IGAMESTATE_H
#define GAME_ENGINE_IGAMESTATE_H

#include "Action.h"

#include "Pos.h"
#include "content/tech/TechDB.h"
#include "core/Ids.h"
#include "systems/CityRewardSystem.h"
#include "terrain/BuildingTypeEnum.h"

struct IGameState {
    virtual ~IGameState() = default;
    virtual PlayerId currentPlayer() const = 0;
    virtual bool isTerminal() const = 0;
    virtual float evaluate(PlayerId forPlayer) const = 0;

    virtual std::vector<Action> legalActions(PlayerId pid) const = 0;
    virtual void apply(const Action& a) = 0;
    virtual void undo(const Action& a) = 0; // albo clone() jeśli wolisz kopie
    virtual std::unique_ptr<IGameState> clone() const = 0;

    virtual uint64_t hash() const = 0; // dla transposition table
};


#endif //GAME_ENGINE_IGAMESTATE_H