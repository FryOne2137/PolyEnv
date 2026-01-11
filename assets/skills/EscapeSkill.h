#ifndef GAME_ENGINE_ESCAPESKILL_H
#define GAME_ENGINE_ESCAPESKILL_H

#include "Skill.h"

class EscapeSkill : public Skill {
public:
    EscapeSkill();
    void apply() override;
};

#endif // GAME_ENGINE_ESCAPESKILL_H
