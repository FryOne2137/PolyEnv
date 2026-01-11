#ifndef GAME_ENGINE_DOUBLEATTACKSKILL_H
#define GAME_ENGINE_DOUBLEATTACKSKILL_H

#include "Skill.h"

class DoubleAttackSkill : public Skill {
public:
    DoubleAttackSkill();
    void apply() override;
};

#endif // GAME_ENGINE_DOUBLEATTACKSKILL_H
