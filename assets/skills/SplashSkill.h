#ifndef GAME_ENGINE_SPLASHSKILL_H
#define GAME_ENGINE_SPLASHSKILL_H

#include "Skill.h"

class SplashSkill : public Skill {
public:
    SplashSkill();
    void apply() override;
};

#endif // GAME_ENGINE_SPLASHSKILL_H
