//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#ifndef GAME_ENGINE_MAP_H
#define GAME_ENGINE_MAP_H

#include <cstdint>
#include <vector>
#include <array>
#include "../content/tribes/Tribe.h"

#include "Tile.h"
#include "Pos.h"

class Map {
public:
    using UnitId = uint16_t;
    static constexpr UnitId kNoUnit = 0xFFFF;

    Map() = default;
    Map(int w, int h) { init(w, h); }

    // Tribes participating in this map (2–16 players)
    void setActiveTribes(const std::vector<TribeType>& tribes) { activeTribes = tribes; }
    const std::vector<TribeType>& getActiveTribes() const { return activeTribes; }

    void init(int w, int h) {
        width = w;
        height = h;
        tiles.assign(static_cast<size_t>(w) * static_cast<size_t>(h), Tile{});
        unitAt.assign(static_cast<size_t>(w) * static_cast<size_t>(h), kNoUnit);
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    bool inBounds(Pos p) const {
        return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height;
    }

    int index(Pos p) const { return p.y * width + p.x; }

    Tile& at(Pos p) { return tiles[static_cast<size_t>(index(p))]; }
    const Tile& at(Pos p) const { return tiles[static_cast<size_t>(index(p))]; }

    UnitId unitOn(Pos p) const { return unitAt[static_cast<size_t>(index(p))]; }
    void setUnitOn(Pos p, UnitId id) { unitAt[static_cast<size_t>(index(p))] = id; }

    const std::vector<Tile>& allTiles() const { return tiles; }
    std::vector<Tile>& allTiles() { return tiles; }

private:
    int width = 0;
    int height = 0;

    std::vector<Tile> tiles;
    std::vector<UnitId> unitAt;
    std::vector<TribeType> activeTribes;
};

#endif //GAME_ENGINE_MAP_H