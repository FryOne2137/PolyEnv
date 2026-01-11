//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#ifndef GAME_ENGINE_UNIT_H
#define GAME_ENGINE_UNIT_H

#include <string>
#include <vector>
#include <memory>

#include "Skill.h"

class Unit {
public:
    Unit(std::string name, int health,bool isVeteran, int attack, int defense, int movement);
    virtual ~Unit() = default;

};

#endif // GAME_ENGINE_UNIT_H