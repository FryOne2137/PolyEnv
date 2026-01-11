#ifndef GAME_ENGINE_FORTIFYSKILL_H
#define GAME_ENGINE_FORTIFYSKILL_H

#include "Skill.h"

class FortifySkill : public Skill {
public:
    FortifySkill();
    void apply() override;
};

#endif // GAME_ENGINE_FORTIFYSKILL_H
