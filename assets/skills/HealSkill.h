#ifndef GAME_ENGINE_HEALSKILL_H
#define GAME_ENGINE_HEALSKILL_H

#include "Skill.h"

class HealSkill : public Skill {
public:
    HealSkill();
    void apply() override;
};

#endif // GAME_ENGINE_HEALSKILL_H
