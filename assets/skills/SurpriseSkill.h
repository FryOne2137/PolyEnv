#ifndef GAME_ENGINE_SURPRISESKILL_H
#define GAME_ENGINE_SURPRISESKILL_H

#include "Skill.h"

class SurpriseSkill : public Skill {
public:
    SurpriseSkill();
    void apply() override;
};

#endif // GAME_ENGINE_SURPRISESKILL_H
