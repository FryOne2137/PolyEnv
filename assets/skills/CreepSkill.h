#ifndef GAME_ENGINE_CREEPSKILL_H
#define GAME_ENGINE_CREEPSKILL_H

#include "Skill.h"

class CreepSkill : public Skill {
public:
    CreepSkill();
    void apply() override;
};

#endif // GAME_ENGINE_CREEPSKILL_H
