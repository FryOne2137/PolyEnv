//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//
#include "Player/Player.h"
#include "../assets/tech/TechDB.h"
#include "Core/Ids.h"
#include "Game.h"

#ifndef GAME_ENGINE_TECHSYSTEM_H
#define GAME_ENGINE_TECHSYSTEM_H


class TechSystem {
public:
    static bool hasTech(const Player& pl, TechId tech);
    static const std::vector<TechId>& getTechs(const Player& pl);

    static bool canBuyTech(const Game& game, PlayerId pid, TechId tech);
    static bool buyTech(Game& game, PlayerId pid, TechId tech);
};


#endif //GAME_ENGINE_TECHSYSTEM_H