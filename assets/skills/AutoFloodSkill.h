#ifndef GAME_ENGINE_AUTOFLOODSKILL_H
#define GAME_ENGINE_AUTOFLOODSKILL_H

#include "Skill.h"

class AutoFloodSkill : public Skill {
public:
    AutoFloodSkill();
    void apply() override;
};

#endif // GAME_ENGINE_AUTOFLOODSKILL_H
