#ifndef GAME_ENGINE_EXPLODESKILL_H
#define GAME_ENGINE_EXPLODESKILL_H

#include "Skill.h"

class ExplodeSkill : public Skill {
public:
    ExplodeSkill();
    void apply() override;
};

#endif // GAME_ENGINE_EXPLODESKILL_H
