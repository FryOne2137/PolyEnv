#ifndef GAME_ENGINE_INFILTRATESKILL_H
#define GAME_ENGINE_INFILTRATESKILL_H

#include "Skill.h"

class InfiltrateSkill : public Skill {
public:
    InfiltrateSkill();
    void apply() override;
};

#endif // GAME_ENGINE_INFILTRATESKILL_H
