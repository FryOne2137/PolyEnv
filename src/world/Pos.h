//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_POS_H
#define GAME_ENGINE_POS_H


struct Pos {
    int x = 0;
    int y = 0;

    Pos() = default;
    Pos(int x, int y) : x(x), y(y) {}

    bool operator==(const Pos& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Pos& other) const {
        return !(*this == other);
    }

    Pos operator+(const Pos& other) const {
        return { x + other.x, y + other.y };
    }

    Pos operator-(const Pos& other) const {
        return { x - other.x, y - other.y };
    }
};

#endif //GAME_ENGINE_POS_H