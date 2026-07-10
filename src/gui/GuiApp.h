//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#ifndef GAME_ENGINE_GUIAPP_H
#define GAME_ENGINE_GUIAPP_H

#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "TextureStore.h"
#include "TribeSelectScreen.h"
#include "MapRenderer.h"

#include "../game/Game.h"
#include "../content/tribes/Tribe.h"
#include "../ai/ModelClient.h"
#include "../replay/ReplayRecorder.h"

class GuiApp {
public:
    GuiApp();

    int run();

private:
    TextureStore* textures = nullptr;

    void startGameWithTribes(const std::vector<TribeType>& tribes);
    bool runRandomAutoActionStep();
    bool loadReplayFile(const std::string& path);
    bool saveReplayFile(const std::string& path);
    bool advanceReplayMove();
    void seekReplayMove(size_t move);
    void recordActionId(size_t actionId);
    void configureRenderer(bool replayViewer);
    bool handleFileUiEvent(const sf::Event& ev);
    void drawFileUi();
    void layoutFileUi();

    // Run one bot action step for the current player.
    // Returns true if an action was applied.
    bool runBotActionStep();

private:
    sf::RenderWindow window;
    sf::Font font;

    enum class Mode { SelectTribes, InGame, ReplayViewer };
    Mode mode = Mode::SelectTribes;

    TribeSelectScreen selectScreen;
    std::unique_ptr<MapRenderer> mapRenderer;

    Game game;
    bool autoRandomEnabled = false;
    sf::Clock autoRandomClock;
    std::mt19937 rng;
    Game::NewGameConfig currentGameConfig_;
    ReplayRecorder replayRecorder_;

    std::vector<Game> replayFrames_;
    size_t replayMove_ = 0;
    bool replayAutoPlayEnabled_ = false;
    sf::Clock replayClock_;

    enum class FileDialogMode { None, LoadReplay, SaveReplay };
    bool fileMenuOpen_ = false;
    FileDialogMode fileDialogMode_ = FileDialogMode::None;
    std::string filePathBuffer_;
    std::string fileStatus_;
    sf::FloatRect fileMenuRect_{8.f, 6.f, 72.f, 28.f};
    sf::FloatRect fileLoadRect_{8.f, 34.f, 190.f, 30.f};
    sf::FloatRect fileSaveRect_{8.f, 64.f, 190.f, 30.f};

    // Bot support
    std::vector<bool> playerIsBot_;       // indexed by player id 0..N-1
    std::unique_ptr<ModelClient> modelClient_;
    sf::Clock botClock_;                  // throttle: 200 ms between bot actions
};

#endif //GAME_ENGINE_GUIAPP_H
