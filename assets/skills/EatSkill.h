#ifndef GAME_ENGINE_EATSKILL_H
#define GAME_ENGINE_EATSKILL_H

#include "Skill.h"

class EatSkill : public Skill {
public:
    EatSkill();
    void apply() override;
};

#endif // GAME_ENGINE_EATSKILL_H
