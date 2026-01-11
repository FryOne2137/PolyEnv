#ifndef GAME_ENGINE_POISONSKILL_H
#define GAME_ENGINE_POISONSKILL_H

#include "Skill.h"

class PoisonSkill : public Skill {
public:
    PoisonSkill();
    void apply() override;
};

#endif // GAME_ENGINE_POISONSKILL_H
