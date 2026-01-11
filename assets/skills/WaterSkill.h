#ifndef GAME_ENGINE_WATERSKILL_H
#define GAME_ENGINE_WATERSKILL_H

#include "Skill.h"

class WaterSkill : public Skill {
public:
    WaterSkill();
    void apply() override;
};

#endif // GAME_ENGINE_WATERSKILL_H
