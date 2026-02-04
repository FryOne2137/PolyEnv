//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "GuiApp.h"

#include <iostream>            // std::cerr
#include <vector>
#include <algorithm>           // std::clamp
#include <chrono>
#include <filesystem>
#include <array>
#include <SFML/Graphics/Image.hpp>

#include "MapRenderer.h"

static bool existsFile(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(p), ec);
}

static bool loadAnyFont(sf::Font& font) {
    // CLion often runs the executable from cmake-build-*/ so relative paths like "assets/..." won't work.
    // Try a few parent prefixes to reach the project root.
    static const std::array<std::string, 6> kPrefixes = {"", "../", "../../", "../../../", "../../../../", "../../../../../"};

    const std::vector<std::string> candidates = {
        // Project fonts
        "assets/textures/josefin-sans.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "assets/fonts/arial.ttf",
        "assets/fonts/DejaVuSans.ttf",

        // macOS system fonts (should exist)
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica.ttf",
        "/Library/Fonts/Arial.ttf",
        "/Library/Fonts/Helvetica.ttf",
    };

    for (const auto& p : candidates) {
        // Absolute paths: try directly
        if (!p.empty() && p[0] == '/') {
            if (existsFile(p) && font.loadFromFile(p)) return true;
            continue;
        }

        // Relative paths: try with prefixes
        for (const auto& pre : kPrefixes) {
            const std::string candidate = pre + p;
            if (existsFile(candidate) && font.loadFromFile(candidate)) return true;
        }
    }

    return false;
}

static bool loadAnyWindowIcon(sf::Image& img) {
    static const std::array<std::string, 6> kPrefixes = {"", "../", "../../", "../../../", "../../../../", "../../../../../"};

    const std::string rel = "assets/Polytopia_game_engine_textures/tribes/Bardur/head.png";

    // Absolute path support (just in case)
    if (!rel.empty() && rel[0] == '/') {
        if (existsFile(rel) && img.loadFromFile(rel)) return true;
    }

    for (const auto& pre : kPrefixes) {
        const std::string candidate = pre + rel;
        if (existsFile(candidate) && img.loadFromFile(candidate)) return true;
    }

    return false;
}

int GuiApp::run() {
    window.create(sf::VideoMode(1280, 720), "Polytopia Debug GUI (SFML)");
    {
        sf::Image icon;
        if (loadAnyWindowIcon(icon)) {
            window.setIcon(icon.getSize().x, icon.getSize().y, icon.getPixelsPtr());
        } else {
            std::cerr << "[ui] WARNING: window icon not found at assets/Polytopia_game_engine_textures/tribes/Vengir/head.png\n";
        }
    }
    window.setFramerateLimit(60);

    if (!loadAnyFont(font)) {
        std::cerr << "ERROR: missing font. Put one at assets/fonts/Roboto-Regular.ttf\n";
        // bez fonta nadal można renderować mapę, ale menu nie będzie widoczne.
    }

    selectScreen.setFont(&font);

    while (window.isOpen()) {
        sf::Event ev{};
        while (window.pollEvent(ev)) {
            if (mode == Mode::InGame && mapRenderer) {
                mapRenderer->handleEvent(ev);

                // UI actions coming from the in-game renderer
                if (mapRenderer->consumeEndTurnClicked()) {
                    game.endTurn(game.getCurrentPlayerId()); // rotates currentPlayer by id
                }
                if (mapRenderer->consumeToggleOverviewRequested()) {
                    mapRenderer->toggleOverview();
                }
            }
            if (ev.type == sf::Event::Closed) window.close();
            if (ev.type == sf::Event::KeyPressed && ev.key.code == sf::Keyboard::Escape) {
                if (mode == Mode::InGame) {
                    // Back to TribeSelectScreen: kill the old world completely
                    mapRenderer.reset();
                    game = Game();              // reset whole game state
                    mode = Mode::SelectTribes;
                } else {
                    window.close();             // ESC closes only in TribeSelectScreen
                }
            }
            if (mode == Mode::SelectTribes) {
                selectScreen.handleEvent(ev, window);

                if (selectScreen.consumeStartClicked()) {
                    const auto tribes = selectScreen.getSelectedTribes();
                    if (tribes.size() >= 2 && tribes.size() <= 16) {
                        mapRenderer.reset();
                        game = Game();
                        startGameWithTribes(tribes);
                        mode = Mode::InGame;
                    }
                }
            } else {
                // in-game input (na razie minimal)
            }
        }

        window.clear(sf::Color(20, 20, 20));

        if (mode == Mode::SelectTribes) {
            selectScreen.draw(window);
        } else {
            if (mapRenderer) {
                mapRenderer->draw(window);
            }
        }

        window.display();
    }

    return 0;
}

void GuiApp::startGameWithTribes(const std::vector<TribeType>& tribes) {
    Game::NewGameConfig cfg;
    cfg.mapSize = selectScreen.getMapSize();
    if (cfg.mapSize < 11) cfg.mapSize = 11;
    cfg.tribes = tribes;

    // Map-gen params from sliders
    // Most map generators expect:
    // - initialLand in 0..1 (float)
    // - smoothing/relief in a SMALL integer range (often 0..10)
    const int uiInitialLand = selectScreen.getInitialLand(); // 0..100
    const int uiSmoothing   = selectScreen.getSmoothing();   // 0..100
    const int uiRelief      = selectScreen.getRelief();      // 0..100

    // Slider mapping: 50 == default values
    // defaults: initialLand=0.5f, smoothing=3, relief=4

    // initialLand: linear 0..100 -> 0..1 (50 -> 0.5)
    cfg.initialLand = static_cast<float>(uiInitialLand) / 100.f;

    // smoothing: map 0..100 -> 0..6  (50 -> 3)
    cfg.smoothing = std::clamp((uiSmoothing * 6 + 50) / 100, 0, 6);

    // relief: map 0..100 -> 0..8    (50 -> 4)
    cfg.relief = std::clamp((uiRelief * 8 + 50) / 100, 0, 8);

    std::cerr << "[mapgen] initialLand=" << cfg.initialLand
              << " smoothing=" << cfg.smoothing
              << " relief=" << cfg.relief
              << " seed=" << static_cast<uint32_t>(selectScreen.getMapSeed())
              << "\n";

    cfg.seed = static_cast<uint32_t>(selectScreen.getMapSeed());
    std::cerr << "[mapgen] seed=" << cfg.seed << " (0=random)\n";

    using clock = std::chrono::high_resolution_clock;
    const auto t0 = clock::now();

    game.newGame(cfg);

    const auto t1 = clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cerr << "[mapgen] generation took " << ms << " ms\n";

    mapRenderer = std::make_unique<MapRenderer>(*textures);
    mapRenderer->setGame(&game);

    // możesz tu od razu dopasować zoom / tile size:
    mapRenderer->setTileSizePx(48);
    mapRenderer->setOrigin({24.f, 24.f});
}

GuiApp::GuiApp() {
    textures = &TextureStore::instance();
}