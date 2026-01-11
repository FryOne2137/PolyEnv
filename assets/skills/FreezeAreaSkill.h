#ifndef GAME_ENGINE_FREEZEAREASKILL_H
#define GAME_ENGINE_FREEZEAREASKILL_H

#include "Skill.h"

class FreezeAreaSkill : public Skill {
public:
    FreezeAreaSkill();
    void apply() override;
};

#endif // GAME_ENGINE_FREEZEAREASKILL_H
