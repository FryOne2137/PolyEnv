#ifndef GAME_ENGINE_DASHSKILL_H
#define GAME_ENGINE_DASHSKILL_H

#include "Skill.h"

class DashSkill : public Skill {
public:
    DashSkill();
    void apply() override;
};

#endif // GAME_ENGINE_DASHSKILL_H
