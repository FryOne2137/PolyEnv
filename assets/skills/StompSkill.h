#ifndef GAME_ENGINE_STOMPSKILL_H
#define GAME_ENGINE_STOMPSKILL_H

#include "Skill.h"

class StompSkill : public Skill {
public:
    StompSkill();
    void apply() override;
};

#endif // GAME_ENGINE_STOMPSKILL_H
