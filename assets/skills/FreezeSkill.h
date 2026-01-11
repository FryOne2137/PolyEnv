#ifndef GAME_ENGINE_FREEZESKILL_H
#define GAME_ENGINE_FREEZESKILL_H

#include "Skill.h"

class FreezeSkill : public Skill {
public:
    FreezeSkill();
    void apply() override;
};

#endif // GAME_ENGINE_FREEZESKILL_H
