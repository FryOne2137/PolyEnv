#ifndef GAME_ENGINE_AIRSKILL_H
#define GAME_ENGINE_AIRSKILL_H

#include "Skill.h"

class AirSkill : public Skill {
public:
    AirSkill();
    void apply() override;
};

#endif // GAME_ENGINE_AIRSKILL_H
