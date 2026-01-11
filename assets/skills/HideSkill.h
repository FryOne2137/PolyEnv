#ifndef GAME_ENGINE_HIDESKILL_H
#define GAME_ENGINE_HIDESKILL_H

#include "Skill.h"

class HideSkill : public Skill {
public:
    HideSkill();
    void apply() override;
};

#endif // GAME_ENGINE_HIDESKILL_H
