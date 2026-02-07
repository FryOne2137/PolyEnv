//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//


#ifndef GAME_ENGINE_TECHSYSTEM_H
#define GAME_ENGINE_TECHSYSTEM_H

#include "Player/Player.h"
#include "../assets/tech/TechDB.h"
#include "Core/Ids.h"
#include "Game.h"


class TechSystem {
public:
    static bool hasTech(const Game &game, PlayerId pid, TechId tech);
    static const std::vector<TechId>& getTechs(const Game &game, PlayerId pid);

    static bool canBuyTech(const Game& game, PlayerId pid, TechId tech);
    static bool buyTech(Game& game, PlayerId pid, TechId tech);
};


#endif //GAME_ENGINE_TECHSYSTEM_H