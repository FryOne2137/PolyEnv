//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_TILE_H
#define GAME_ENGINE_TILE_H
#include "terrain/baseTerrainEnum.h"
#include "terrain/ResourcesEnum.h"


class Tile {
public:


    ResourcesEnum getResource();
    BaseTerrainEnum getBaseTerrain();
    int getBiome();


private:


    ResourcesEnum resource;
    BaseTerrainEnum baseTerrain;
    int biome;


};


#endif //GAME_ENGINE_TILE_H