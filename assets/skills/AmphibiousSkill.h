#ifndef GAME_ENGINE_AMPHIBIOUSSKILL_H
#define GAME_ENGINE_AMPHIBIOUSSKILL_H

#include "Skill.h"

class AmphibiousSkill : public Skill {
public:
    AmphibiousSkill();
    void apply() override;
};

#endif // GAME_ENGINE_AMPHIBIOUSSKILL_H
