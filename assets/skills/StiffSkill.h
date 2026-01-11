#ifndef GAME_ENGINE_STIFFSKILL_H
#define GAME_ENGINE_STIFFSKILL_H

#include "Skill.h"

class StiffSkill : public Skill {
public:
    StiffSkill();
    void apply() override;
};

#endif // GAME_ENGINE_STIFFSKILL_H
