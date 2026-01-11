#ifndef GAME_ENGINE_SKATESKILL_H
#define GAME_ENGINE_SKATESKILL_H

#include "Skill.h"

class SkateSkill : public Skill {
public:
    SkateSkill();
    void apply() override;
};

#endif // GAME_ENGINE_SKATESKILL_H
