//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#ifndef GAME_ENGINE_GENERATEMAP_H
#define GAME_ENGINE_GENERATEMAP_H


#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct Tile {
    std::string type;   // "ocean", "water", "ground", "forest", "mountain"
    std::string above;  // "", "capital", "village", "ruin", "fruit", "crop", "game", "fish", "whale", "metal"
    bool road = false;
    std::string tribe;  // np. "Xin-xi"
};

struct MapConfig {
    int mapSize = 16;
    double initialLand = 0.5;
    int smoothing = 3;
    int relief = 4;
    std::vector<std::string> tribes = {"Vengir", "Bardur", "Oumaji"};

    // seed opcjonalny: jak podasz, wynik będzie deterministyczny
    bool useSeed = false;
    uint32_t seed = 0;
};

struct GeneratedMap {
    int mapSize = 0;
    std::vector<Tile> world;         // rozmiar mapSize^2
    std::vector<int> villageMap;     // -1 / 0..3 jak w pythonie
    std::vector<int> capitalCells;   // indeksy pól z kapitalami
};

// Główna funkcja generatora
GeneratedMap generateMap(const MapConfig& cfg);

// --- Utilsy gridowe (odpowiedniki z Pythona) ---
std::vector<int> circle(int center, int radius, int mapSize);
std::vector<int> round_(int center, int radius, int mapSize);
int distanceChebyshev(int a, int b, int size);
std::vector<int> plusSign(int center, int mapSize);

#endif //GAME_ENGINE_GENERATEMAP_H