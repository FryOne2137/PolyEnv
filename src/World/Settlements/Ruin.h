#ifndef GAME_ENGINE_RUIN_H
#define GAME_ENGINE_RUIN_H
#include "Settlement.h"

class Ruin : public Settlement {
    // opcjonalnie: flaga czy już zużyte
public:
    bool used = false;
};

#endif