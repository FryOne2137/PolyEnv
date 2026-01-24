//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_TEXTURESTORE_H
#define GAME_ENGINE_TEXTURESTORE_H


#include <SFML/Graphics.hpp>
#include <unordered_map>
#include <string>

class TextureStore {
public:
    static TextureStore& instance();

    // Zwraca texture; jeśli brak pliku -> zwraca “missing” (magenta checker)


    const sf::Texture& get(const std::string& path);
    void setFont(sf::Font* f);
    void handleEvent(const sf::Event& ev, const sf::RenderWindow& window);
    void draw(sf::RenderTarget& rt);


private:
    TextureStore() = default;
    TextureStore(const TextureStore&) = delete;
    TextureStore& operator=(const TextureStore&) = delete;

    const sf::Texture& missing();
    sf::Font* font = nullptr;

private:
    std::unordered_map<std::string, sf::Texture> cache;
    sf::Texture missingTex;
    bool missingBuilt = false;
};

#endif //GAME_ENGINE_TEXTURESTORE_H