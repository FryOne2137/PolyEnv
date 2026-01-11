#ifndef GAME_ENGINE_SNEAKSKILL_H
#define GAME_ENGINE_SNEAKSKILL_H

#include "Skill.h"

class SneakSkill : public Skill {
public:
    SneakSkill();
    void apply() override;
};

#endif // GAME_ENGINE_SNEAKSKILL_H
