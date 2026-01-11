#ifndef GAME_ENGINE_TENTACLESSKILL_H
#define GAME_ENGINE_TENTACLESSKILL_H

#include "Skill.h"

class TentaclesSkill : public Skill {
public:
    TentaclesSkill();
    void apply() override;
};

#endif // GAME_ENGINE_TENTACLESSKILL_H
