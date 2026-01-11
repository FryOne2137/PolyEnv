#ifndef GAME_ENGINE_CONVERTSKILL_H
#define GAME_ENGINE_CONVERTSKILL_H

#include "Skill.h"

class ConvertSkill : public Skill {
public:
    ConvertSkill();
    void apply() override;
};

#endif // GAME_ENGINE_CONVERTSKILL_H
