//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "GuiApp.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <array>
#include <random>
#include <SFML/Graphics/Image.hpp>

#include "MapRenderer.h"
#include "ai/GameStateAdapter.h"
#include "ai/ModelClient.h"

static bool existsFile(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(p), ec);
}

static bool loadAnyFont(sf::Font& font) {
    static const std::array<std::string, 6> kPrefixes = {"", "../", "../../", "../../../", "../../../../", "../../../../../"};

    const std::vector<std::string> candidates = {
        "assets/textures/josefin-sans.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "assets/fonts/arial.ttf",
        "assets/fonts/DejaVuSans.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica.ttf",
        "/Library/Fonts/Arial.ttf",
        "/Library/Fonts/Helvetica.ttf",
    };

    for (const auto& p : candidates) {
        if (!p.empty() && p[0] == '/') {
            if (existsFile(p) && font.loadFromFile(p)) return true;
            continue;
        }
        for (const auto& pre : kPrefixes) {
            const std::string candidate = pre + p;
            if (existsFile(candidate) && font.loadFromFile(candidate)) return true;
        }
    }
    return false;
}

static bool loadAnyWindowIcon(sf::Image& img) {
    static const std::array<std::string, 6> kPrefixes = {"", "../", "../../", "../../../", "../../../../", "../../../../../"};
    const std::string rel = "assets/textures/Polytopia_game_engine_textures/tribes/Bardur/head.png";
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
            std::cerr << "[ui] WARNING: window icon not found\n";
        }
    }
    window.setFramerateLimit(60);

    if (!loadAnyFont(font)) {
        std::cerr << "ERROR: missing font. Put one at assets/fonts/Roboto-Regular.ttf\n";
    }

    selectScreen.setFont(&font);

    while (window.isOpen()) {
        sf::Event ev{};
        while (window.pollEvent(ev)) {
            if (mode == Mode::InGame && mapRenderer) {
                mapRenderer->handleEvent(ev);

                if (mapRenderer->consumeEndTurnClicked()) {
                    game.endTurn(game.getCurrentPlayerId());
                    botClock_.restart();
                }
                if (mapRenderer->consumeToggleOverviewRequested()) {
                    mapRenderer->toggleOverview();
                }
                if (mapRenderer->consumeAutoPlayToggleRequested()) {
                    autoRandomEnabled = !autoRandomEnabled;
                    mapRenderer->setAutoPlayActive(autoRandomEnabled);
                    autoRandomClock.restart();
                }
            }
            if (ev.type == sf::Event::Closed) window.close();
            if (ev.type == sf::Event::KeyPressed && ev.key.code == sf::Keyboard::Escape) {
                if (mode == Mode::InGame) {
                    mapRenderer.reset();
                    game = Game();
                    autoRandomEnabled = false;
                    playerIsBot_.clear();
                    modelClient_.reset();
                    mode = Mode::SelectTribes;
                } else {
                    window.close();
                }
            }
            if (mode == Mode::SelectTribes) {
                selectScreen.handleEvent(ev, window);

                if (selectScreen.consumeStartClicked()) {
                    const auto tribes = selectScreen.getSelectedTribes();
                    if (tribes.size() >= 2 && tribes.size() <= 16) {
                        mapRenderer.reset();
                        game = Game();
                        autoRandomEnabled = false;
                        startGameWithTribes(tribes);
                        mode = Mode::InGame;
                        botClock_.restart();
                    }
                }
            }
        }

        // ── Auto-random (human demo mode) ─────────────────────────────────────
        if (mode == Mode::InGame && mapRenderer && autoRandomEnabled) {
            if (game.isGameOver()) {
                autoRandomEnabled = false;
                mapRenderer->setAutoPlayActive(false);
            } else if (autoRandomClock.getElapsedTime().asSeconds() >= 0.2f) {
                autoRandomClock.restart();
                if (!runRandomAutoActionStep()) {
                    autoRandomEnabled = false;
                    mapRenderer->setAutoPlayActive(false);
                }
            }
        }

        // ── Bot turn ──────────────────────────────────────────────────────────
        if (mode == Mode::InGame && !autoRandomEnabled && !game.isGameOver() && modelClient_) {
            const PlayerId pid = game.getCurrentPlayerId();
            const bool isBot   = (static_cast<size_t>(pid) < playerIsBot_.size())
                                 && playerIsBot_[static_cast<size_t>(pid)];

            if (isBot && botClock_.getElapsedTime().asMilliseconds() >= 200) {
                botClock_.restart();
                if (!runBotActionStep()) {
                    // Bot couldn't act (no legal moves or error) — stop bot loop
                    std::cerr << "[bot] No action returned; skipping.\n";
                }
            }
        }

        // ── Render ────────────────────────────────────────────────────────────
        window.clear(sf::Color(20, 20, 20));
        if (mode == Mode::SelectTribes) {
            selectScreen.draw(window);
        } else {
            if (mapRenderer) mapRenderer->draw(window);
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

    const int uiInitialLand = selectScreen.getInitialLand();
    const int uiSmoothing   = selectScreen.getSmoothing();
    const int uiRelief      = selectScreen.getRelief();

    cfg.initialLand = static_cast<float>(uiInitialLand) / 100.f;
    cfg.smoothing   = std::clamp((uiSmoothing * 6 + 50) / 100, 0, 6);
    cfg.relief      = std::clamp((uiRelief * 8 + 50) / 100, 0, 8);
    cfg.seed        = static_cast<uint32_t>(selectScreen.getMapSeed());

    std::cerr << "[mapgen] initialLand=" << cfg.initialLand
              << " smoothing=" << cfg.smoothing
              << " relief=" << cfg.relief
              << " seed=" << cfg.seed << "\n";

    using clock = std::chrono::high_resolution_clock;
    const auto t0 = clock::now();
    game.newGame(cfg);
    const auto t1 = clock::now();
    std::cerr << "[mapgen] generation took "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms\n";

    mapRenderer = std::make_unique<MapRenderer>(*textures);
    mapRenderer->setGame(&game);
    mapRenderer->setAutoPlayActive(false);
    autoRandomClock.restart();
    mapRenderer->setTileSizePx(48);
    mapRenderer->setOrigin({24.f, 24.f});

    // ── Bot configuration ─────────────────────────────────────────────────────
    const auto& bots = selectScreen.getSelectedBots();
    playerIsBot_.assign(tribes.size(), false);
    bool anyBot = false;
    for (size_t i = 0; i < bots.size() && i < tribes.size(); ++i) {
        playerIsBot_[i] = bots[i];
        if (bots[i]) anyBot = true;
    }

    modelClient_.reset();
    if (anyBot) {
        const int port         = selectScreen.getServerPort();
        const std::string addr = "tcp://localhost:" + std::to_string(port);
        std::cerr << "[bot] Connecting to inference server at " << addr << "\n";
        try {
            modelClient_ = std::make_unique<ModelClient>(addr);
        } catch (const std::exception& e) {
            std::cerr << "[bot] ERROR creating ModelClient: " << e.what() << "\n";
        }
    }
}

GuiApp::GuiApp() {
    textures = &TextureStore::instance();
    rng.seed(std::random_device{}());
}

bool GuiApp::runRandomAutoActionStep() {
    if (game.isGameOver()) return false;

    GameStateAdapter adapter(game);
    const PlayerId pid = adapter.currentPlayer();
    const std::vector<uint8_t> mask = adapter.legalActionMask(pid);

    std::vector<size_t> legalIds;
    for (size_t i = 0; i < mask.size(); ++i)
        if (mask[i]) legalIds.push_back(i);
    if (legalIds.empty()) return false;

    std::uniform_int_distribution<size_t> dist(0, legalIds.size() - 1);
    const size_t chosen = legalIds[dist(rng)];
    const std::optional<Action> action = adapter.decodeActionId(pid, chosen);
    if (!action) return false;

    adapter.apply(*action);
    game = adapter.getGame();
    if (mapRenderer) mapRenderer->clearSelection();
    return true;
}

bool GuiApp::runBotActionStep() {
    if (game.isGameOver() || !modelClient_) return false;

    GameStateAdapter adapter(game);
    const PlayerId pid = adapter.currentPlayer();

    // 1. Send full state → server returns one action_id (argmax)
    int actionId = -1;
    try {
        actionId = modelClient_->queryAction(game, adapter);
    } catch (const BotClientError& e) {
        std::cerr << "[bot] " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[bot] Unexpected error: " << e.what() << "\n";
        return false;
    }

    // 2. Decode the action (server picks from legal list → always valid)
    const std::optional<Action> action =
        adapter.decodeActionId(pid, static_cast<size_t>(actionId));
    if (!action) {
        std::cerr << "[bot] action_id=" << actionId << " not legal — skipping step\n";
        return false;
    }

    // 3. Apply and update game state
    adapter.apply(*action);
    game = adapter.getGame();
    if (mapRenderer) mapRenderer->clearSelection();
    return true;
    // 4. Loop: called again in 200ms with the new state
}
