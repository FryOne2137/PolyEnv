//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#ifndef GAME_ENGINE_ALGAESKILL_H
#define GAME_ENGINE_ALGAESKILL_H

#include "Skill.h"

class AlgaeSkill : public Skill {
public:
    AlgaeSkill();
    void apply();
};

#endif //GAME_ENGINE_ALGAESKILL_H