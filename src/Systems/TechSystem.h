//
// Created by Fryderyk Niedzwiecki on 24/01/2026.
//
#include "Player/Player.h"
#include "../assets/tech/TechDB.h"

#ifndef GAME_ENGINE_TECHSYSTEM_H
#define GAME_ENGINE_TECHSYSTEM_H


class TechSystem {
public:
    static bool hasTech(const Player& pl, TechId tech);
    static const std::vector<TechId>& getTechs(const Player& pl);

    static bool canBuyTech(const Player& pl, TechId tech);
    static bool buyTech(Player& pl, TechId tech);
};


#endif //GAME_ENGINE_TECHSYSTEM_H