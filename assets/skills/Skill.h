//
// Created by Fryderyk Niedzwiecki on 09/01/2026.
//

#ifndef GAME_ENGINE_SKILL_H
#define GAME_ENGINE_SKILL_H

#include <string>

class Skill {
public:
    explicit Skill(const std::string& name);
    virtual ~Skill() = default;

    const std::string& getName() const;

    virtual void apply() = 0;

protected:
    std::string name;
};

#endif // GAME_ENGINE_SKILL_H