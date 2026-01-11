#ifndef GAME_ENGINE_DRENCHSKILL_H
#define GAME_ENGINE_DRENCHSKILL_H

#include "Skill.h"

class DrenchSkill : public Skill {
public:
    DrenchSkill();
    void apply() override;
};

#endif // GAME_ENGINE_DRENCHSKILL_H
