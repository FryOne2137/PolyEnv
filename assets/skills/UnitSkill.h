//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_UNITSKILL_H
#define GAME_ENGINE_UNITSKILL_H


#include <cstdint>

enum class UnitSkill : uint64_t {
    None            = 0,
    Algae           = 1ULL << 0,  // tworzy algi przy ruchu
    Amphibious      = 1ULL << 1,  // ruch na lądzie i wodzie
    AutoFlood       = 1ULL << 2,  // automatyczne zalewanie
    AutoFreeze      = 1ULL << 3,  // automatyczne zamrażanie
    Swarm           = 1ULL << 4,
    Carry           = 1ULL << 5,
    Convert         = 1ULL << 6,
    Creep           = 1ULL << 7,
    Dash            = 1ULL << 8,
    DoubleAttack    = 1ULL << 9,
    Drench          = 1ULL << 10,
    Eat             = 1ULL << 11,
    Escape          = 1ULL << 12,
    Explode         = 1ULL << 13,
    Air             = 1ULL << 14,
    Fortify         = 1ULL << 15,
    Freeze          = 1ULL << 16,
    FreezeArea      = 1ULL << 17,
    Grow            = 1ULL << 18,
    Heal            = 1ULL << 19,
    Hide            = 1ULL << 20,
    Independent     = 1ULL << 21,
    Infiltrate      = 1ULL << 22,
    Persist         = 1ULL << 23,
    Poison          = 1ULL << 24,
    StaticSkill     = 1ULL << 25,  // statyczna (brak vet)
    Scout           = 1ULL << 26,
    Skate           = 1ULL << 27,
    Sneak           = 1ULL << 28,
    Splash          = 1ULL << 29,
    Stiff           = 1ULL << 30,
    Stomp           = 1ULL << 31,
    Surprise        = 1ULL << 32,
    Tentacles       = 1ULL << 33,
    WaterOnly       = 1ULL << 34
};


#endif //GAME_ENGINE_UNITSKILL_H