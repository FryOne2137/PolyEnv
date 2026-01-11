#ifndef GAME_ENGINE_SCOUTSKILL_H
#define GAME_ENGINE_SCOUTSKILL_H

#include "Skill.h"

class ScoutSkill : public Skill {
public:
    ScoutSkill();
    void apply() override;
};

#endif // GAME_ENGINE_SCOUTSKILL_H
