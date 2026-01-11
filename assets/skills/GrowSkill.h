#ifndef GAME_ENGINE_GROWSKILL_H
#define GAME_ENGINE_GROWSKILL_H

#include "Skill.h"

class GrowSkill : public Skill {
public:
    GrowSkill();
    void apply() override;
};

#endif // GAME_ENGINE_GROWSKILL_H
