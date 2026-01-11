//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#ifndef GAME_ENGINE_TRIBE_H
#define GAME_ENGINE_TRIBE_H
#include <string>

#include "Skill.h"
#include "techs/Tech.h"


class Tribe {
public:
    Tribe(int id, std::string name, int stars, const Tech* startTech);

    int getId();
    int getStartStars();
    const std::string& getName() const;
    const Tech* getStartTech() const;


private:
    int tribeId;
    std::string tribeName;

    int startStars;
    const Tech* startTech = nullptr;
};


#endif //GAME_ENGINE_TRIBE_H