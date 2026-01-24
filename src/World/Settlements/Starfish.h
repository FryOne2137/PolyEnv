#ifndef GAME_ENGINE_STARFISH_H
#define GAME_ENGINE_STARFISH_H
#include "Settlement.h"

class Starfish : public Settlement {
public:
    bool used = false;
    int reward = 8; // dane, nie logika
};

#endif