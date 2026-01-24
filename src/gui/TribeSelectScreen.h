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


#include "tribes/Tribe.h"

class TextureStore;

class TribeSelectScreen {
public:
    int getInitialLand() const;
    int getSmoothing() const;
    int getRelief() const;

    TribeSelectScreen();

    void setFont(sf::Font* f);

    void handleEvent(const sf::Event& ev, const sf::RenderWindow& window);
    void draw(sf::RenderTarget& rt);

    const std::vector<TribeType>& getSelectedTribes() const;
    int getMapSize() const;

    bool consumeStartClicked(); // returns true once

private:
    TextureStore* textureStore = nullptr;

    struct Button {
        sf::FloatRect rect;
        std::string text;
    };

    std::vector<TribeType> allTribes;
    std::vector<TribeType> selected;

    sf::Font* font = nullptr;

    bool startClicked = false;

    Button startBtn;
    Button popBtn;
    Button clearBtn;

    // Map size controls
    int mapSize = 16;              // default map size
    Button mapSizeMinusBtn;
    Button mapSizePlusBtn;

    int hoverIndex = -1;

    static const char* tribeName(TribeType t);
    static const char* tribeFolder(TribeType t);

    int initialLand = 50;
    int smoothing   = 50;
    int relief      = 50;

    enum class ActiveSlider { None, InitialLand, Smoothing, Relief };
    ActiveSlider activeSlider = ActiveSlider::None;
};

#endif //GAME_ENGINE_TRIBESELECTSCREEN_H