#ifndef GAME_ENGINE_INDEPENDENTSKILL_H
#define GAME_ENGINE_INDEPENDENTSKILL_H

#include "Skill.h"

class IndependentSkill : public Skill {
public:
    IndependentSkill();
    void apply() override;
};

#endif // GAME_ENGINE_INDEPENDENTSKILL_H
