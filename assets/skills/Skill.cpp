//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#include "Skill.h"

Skill::Skill(const std::string& name) : name(name) {}

const std::string& Skill::getName() const {
    return name;
}