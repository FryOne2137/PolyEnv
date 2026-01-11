#ifndef GAME_ENGINE_PERSISTSKILL_H
#define GAME_ENGINE_PERSISTSKILL_H

#include "Skill.h"

class PersistSkill : public Skill {
public:
    PersistSkill();
    void apply() override;
};

#endif // GAME_ENGINE_PERSISTSKILL_H
