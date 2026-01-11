//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_ORGANIZATIONTECH_H
#define GAME_ENGINE_ORGANIZATIONTECH_H
#include "Tech.h"


class OrganizationTech : public Tech {
    public:
    OrganizationTech(const Tech* previous);
    OrganizationTech();
    static const OrganizationTech& getBase();

};


#endif //GAME_ENGINE_ORGANIZATIONTECH_H