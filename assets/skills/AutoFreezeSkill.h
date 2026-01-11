#ifndef GAME_ENGINE_AUTOFREEZESKILL_H
#define GAME_ENGINE_AUTOFREEZESKILL_H

#include "Skill.h"

class AutoFreezeSkill : public Skill {
public:
    AutoFreezeSkill();
    void apply() override;
};

#endif // GAME_ENGINE_AUTOFREEZESKILL_H
