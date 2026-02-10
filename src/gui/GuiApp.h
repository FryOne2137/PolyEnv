//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_GUIAPP_H
#define GAME_ENGINE_GUIAPP_H


#pragma once

#include <SFML/Graphics.hpp>   // sf::RenderWindow, sf::Font, sf::Event etc.
#include <memory>
#include <random>
#include <vector>

#include "TextureStore.h"
#include "TribeSelectScreen.h"
#include "MapRenderer.h"

#include "../game/Game.h"
#include "../content/tribes/Tribe.h"

class GuiApp {
public:
    GuiApp();          // <<< TO MUSI TU BYĆ

    int run();

private:
    TextureStore* textures = nullptr;

    void startGameWithTribes(const std::vector<TribeType>& tribes);
    bool runRandomAutoActionStep();

private:
    sf::RenderWindow window;


    // font for UI
    sf::Font font;

    enum class Mode {
        SelectTribes,
        InGame
    };

    Mode mode = Mode::SelectTribes;

    TribeSelectScreen selectScreen;
    std::unique_ptr<MapRenderer> mapRenderer;

    Game game;
    bool autoRandomEnabled = false;
    sf::Clock autoRandomClock;
    std::mt19937 rng;
};

#endif //GAME_ENGINE_GUIAPP_H
