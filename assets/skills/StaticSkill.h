#ifndef GAME_ENGINE_STATICSKILL_H
#define GAME_ENGINE_STATICSKILL_H

#include "Skill.h"

class StaticSkill : public Skill {
public:
    StaticSkill();
    void apply() override;
};

#endif // GAME_ENGINE_STATICSKILL_H
