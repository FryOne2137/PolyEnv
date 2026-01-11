#ifndef GAME_ENGINE_CARRYSKILL_H
#define GAME_ENGINE_CARRYSKILL_H

#include "Skill.h"

class CarrySkill : public Skill {
public:
    CarrySkill();
    void apply() override;
};

#endif // GAME_ENGINE_CARRYSKILL_H
