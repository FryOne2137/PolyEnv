//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "TextureStore.h"

#include <iostream>

TextureStore& TextureStore::instance() {
    static TextureStore inst;
    return inst;
}

const sf::Texture& TextureStore::missing() {
    if (missingBuilt) return missingTex;

    // 2x2 magenta/black
    sf::Image img;
    img.create(2, 2, sf::Color::Magenta);
    img.setPixel(1, 0, sf::Color::Black);
    img.setPixel(0, 1, sf::Color::Black);
    img.setPixel(1, 1, sf::Color::Magenta);

    missingTex.loadFromImage(img);
    missingBuilt = true;
    return missingTex;
}

const sf::Texture& TextureStore::get(const std::string& path) {
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;

    sf::Texture tex;
    if (!tex.loadFromFile(path)) {
        // nie spamuj za mocno — ale to mega pomaga w debug
        std::cerr << "[TextureStore] Missing texture: " << path << "\n";
        return missing();
    }

    tex.setSmooth(true);
    auto [insIt, _] = cache.emplace(path, std::move(tex));
    return insIt->second;
}