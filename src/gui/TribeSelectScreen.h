//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_TRIBESELECTSCREEN_H
#define GAME_ENGINE_TRIBESELECTSCREEN_H

#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include "TextureStore.h"

#include "../content/tribes/Tribe.h"
#include "../world/MapGenerator.h"

class TextureStore;

class TribeSelectScreen {
public:
    MapType getMapType() const;
    int getMapSeed() const;

    TribeSelectScreen();

    void setFont(sf::Font* f);

    void handleEvent(const sf::Event& ev, const sf::RenderWindow& window);
    void draw(sf::RenderTarget& rt);

    const std::vector<TribeType>& getSelectedTribes() const;
    // Parallel to getSelectedTribes(): true = bot, false = human player
    const std::vector<bool>& getSelectedBots() const;

    int getMapSize() const;
    int getServerPort() const;  // port number entered by user (default 5555)

    bool consumeStartClicked(); // returns true once

private:
    TextureStore* textureStore = nullptr;

    struct Button {
        sf::FloatRect rect;
        std::string text;
    };

    std::vector<TribeType> allTribes;
    std::vector<TribeType> selected;
    std::vector<bool>      selectedBots; // parallel to selected; true = bot

    sf::Font* font = nullptr;

    bool startClicked = false;

    Button startBtn;
    Button popBtn;
    Button clearBtn;

    // Map size controls
    int mapSize = 16;
    Button mapSizeMinusBtn;
    Button mapSizePlusBtn;

    int hoverIndex = -1;

    static const char* tribeName(TribeType t);
    static const char* tribeFolder(TribeType t);

    MapType mapType = MapType::Lakes;
    Button lakesBtn;
    Button drylandsBtn;

    int mapSeed = 0;

    // ── bot / port UI state ───────────────────────────────────────────────
    // Index of the selected-list entry currently being toggled (-1 = none)
    int botToggleHover = -1;

    // Port input
    std::string portBuffer = "5555";
    bool portActive = false;
};

#endif //GAME_ENGINE_TRIBESELECTSCREEN_H
