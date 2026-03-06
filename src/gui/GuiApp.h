//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_GUIAPP_H
#define GAME_ENGINE_GUIAPP_H

#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <random>
#include <vector>

#include "TextureStore.h"
#include "TribeSelectScreen.h"
#include "MapRenderer.h"

#include "../game/Game.h"
#include "../content/tribes/Tribe.h"
#include "../ai/ModelClient.h"

class GuiApp {
public:
    GuiApp();

    int run();

private:
    TextureStore* textures = nullptr;

    void startGameWithTribes(const std::vector<TribeType>& tribes);
    bool runRandomAutoActionStep();

    // Run one bot action step for the current player.
    // Returns true if an action was applied.
    bool runBotActionStep();

private:
    sf::RenderWindow window;
    sf::Font font;

    enum class Mode { SelectTribes, InGame };
    Mode mode = Mode::SelectTribes;

    TribeSelectScreen selectScreen;
    std::unique_ptr<MapRenderer> mapRenderer;

    Game game;
    bool autoRandomEnabled = false;
    sf::Clock autoRandomClock;
    std::mt19937 rng;

    // Bot support
    std::vector<bool> playerIsBot_;       // indexed by player id 0..N-1
    std::unique_ptr<ModelClient> modelClient_;
    sf::Clock botClock_;                  // throttle: 200 ms between bot actions
};

#endif //GAME_ENGINE_GUIAPP_H
