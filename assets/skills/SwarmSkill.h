#ifndef GAME_ENGINE_SWARMSKILL_H
#define GAME_ENGINE_SWARMSKILL_H

#include "Skill.h"

class SwarmSkill : public Skill {
public:
    SwarmSkill();
    void apply() override;
};

#endif // GAME_ENGINE_SWARMSKILL_H
