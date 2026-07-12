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
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <SFML/Graphics/Image.hpp>

#include "MapRenderer.h"
#include "ai/GameStateAdapter.h"
#include "ai/ModelClient.h"

namespace {
std::string trimTrailingWhitespace(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> openReplayFileDialog() {
#ifdef __APPLE__
    const char* command =
        "osascript -e 'POSIX path of (choose file with prompt \"Load .polygame replay\" "
        "of type {\"polygame\", \"public.json\"})' 2>/dev/null";
    FILE* pipe = popen(command, "r");
    if (!pipe) return std::nullopt;
    std::string path;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) path += buffer;
    const int status = pclose(pipe);
    path = trimTrailingWhitespace(std::move(path));
    if (status != 0 || path.empty()) return std::nullopt;
    return path;
#else
    return std::nullopt;
#endif
}

std::optional<std::string> saveReplayFileDialog() {
#ifdef __APPLE__
    const char* command =
        "osascript -e 'POSIX path of (choose file name with prompt \"Save .polygame replay\" "
        "default name \"match.polygame\")' 2>/dev/null";
    FILE* pipe = popen(command, "r");
    if (!pipe) return std::nullopt;
    std::string path;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) path += buffer;
    const int status = pclose(pipe);
    path = trimTrailingWhitespace(std::move(path));
    if (status != 0 || path.empty()) return std::nullopt;
    return path;
#else
    return std::nullopt;
#endif
}
} // namespace

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
            if (ev.type == sf::Event::Closed) window.close();
            if (handleFileUiEvent(ev)) continue;

            if ((mode == Mode::InGame || mode == Mode::ReplayViewer) && mapRenderer) {
                mapRenderer->handleEvent(ev);

                if (mode == Mode::InGame && mapRenderer->consumeEndTurnClicked()) {
                    GameStateAdapter adapter(game);
                    const PlayerId pid = adapter.currentPlayer();
                    const std::optional<Action> action = adapter.decodeActionId(pid, 0);
                    const std::vector<uint8_t> legal = adapter.legalActionMask(pid);
                    if (action && !legal.empty() && legal[0]) {
                        adapter.apply(*action);
                        game = adapter.getGame();
                        recordActionId(0);
                    }
                    mapRenderer->notifyGameStateChanged();
                    botClock_.restart();
                }
                if (mapRenderer->consumeToggleOverviewRequested()) {
                    mapRenderer->toggleOverview();
                }
                if (mode == Mode::InGame && mapRenderer->consumeAutoPlayToggleRequested()) {
                    autoRandomEnabled = !autoRandomEnabled;
                    mapRenderer->setAutoPlayActive(autoRandomEnabled);
                    autoRandomClock.restart();
                }
                if (mode == Mode::ReplayViewer && mapRenderer->consumeReplayNextMoveRequested()) {
                    advanceReplayMove();
                }
                if (mode == Mode::ReplayViewer && mapRenderer->consumeReplayAutoPlayToggleRequested()) {
                    replayAutoPlayEnabled_ = !replayAutoPlayEnabled_;
                    mapRenderer->setReplayAutoPlayActive(replayAutoPlayEnabled_);
                    replayClock_.restart();
                }
                if (mode == Mode::ReplayViewer) {
                    if (const auto seek = mapRenderer->consumeReplaySeekRequested()) seekReplayMove(*seek);
                }
            }
            if (ev.type == sf::Event::KeyPressed && ev.key.code == sf::Keyboard::Escape) {
                if (mode == Mode::InGame || mode == Mode::ReplayViewer) {
                    mapRenderer.reset();
                    game = Game();
                    autoRandomEnabled = false;
                    replayAutoPlayEnabled_ = false;
                    replayFrames_.clear();
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

        if (mode == Mode::ReplayViewer && mapRenderer && replayAutoPlayEnabled_) {
            const float interval = mapRenderer->replayIntervalSeconds();
            if (replayClock_.getElapsedTime().asSeconds() >= interval) {
                replayClock_.restart();
                if (!advanceReplayMove()) {
                    replayAutoPlayEnabled_ = false;
                    mapRenderer->setReplayAutoPlayActive(false);
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
        drawFileUi();
        window.display();
    }

    return 0;
}

void GuiApp::startGameWithTribes(const std::vector<TribeType>& tribes) {
    Game::NewGameConfig cfg;
    cfg.mapSize = selectScreen.getMapSize();
    if (cfg.mapSize < 11) cfg.mapSize = 11;
    cfg.tribes = tribes;

    cfg.mapType     = selectScreen.getMapType();
    cfg.seed        = static_cast<uint32_t>(selectScreen.getMapSeed());

    std::cerr << "[mapgen] mapType=" << (cfg.mapType == MapType::Lakes ? "Lakes" : "Drylands")
              << " seed=" << cfg.seed << "\n";

    using clock = std::chrono::high_resolution_clock;
    const auto t0 = clock::now();
    game.newGame(cfg);
    currentGameConfig_ = cfg;
    replayRecorder_.clear();
    const auto t1 = clock::now();
    std::cerr << "[mapgen] generation took "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms\n";

    configureRenderer(false);

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

void GuiApp::configureRenderer(bool replayViewer) {
    mapRenderer = std::make_unique<MapRenderer>(*textures);
    mapRenderer->setGame(&game);
    mapRenderer->setTileSizePx(48);
    mapRenderer->setOrigin({24.f, 24.f});
    mapRenderer->setAutoPlayActive(false);
    mapRenderer->setReplayViewer(replayViewer);
    mapRenderer->setReplayProgress(replayMove_, replayFrames_.empty() ? 0 : replayFrames_.size() - 1);
    mapRenderer->setReplayAutoPlayActive(false);
    mapRenderer->setActionAppliedCallback([this](size_t actionId) {
        recordActionId(actionId);
    });
    autoRandomClock.restart();
    replayClock_.restart();
}

void GuiApp::recordActionId(size_t actionId) {
    if (mode == Mode::InGame) replayRecorder_.record(actionId);
}

bool GuiApp::saveReplayFile(const std::string& path) {
    if (path.empty() || mode != Mode::InGame) return false;
    ReplayMetadata metadata;
    metadata.seed = game.getWorldSeed();
    metadata.mapSize = game.getMap().getWidth();
    metadata.mapType = static_cast<uint8_t>(currentGameConfig_.mapType);
    for (const Player& player : game.getPlayers()) {
        metadata.tribes.push_back(static_cast<int>(player.getTribeType()));
    }

    std::string error;
    if (ReplayRecorder::save(path, metadata, replayRecorder_.actionIds(), error)) {
        fileStatus_ = "Saved " + path;
        return true;
    }
    fileStatus_ = "Save failed: " + error;
    return false;
}

bool GuiApp::loadReplayFile(const std::string& path) {
    ReplayMetadata metadata;
    std::vector<size_t> actionIds;
    std::string error;
    if (!ReplayRecorder::load(path, metadata, actionIds, error)) {
        fileStatus_ = "Load failed: " + error;
        return false;
    }

    try {
        Game::NewGameConfig cfg;
        cfg.seed = metadata.seed;
        cfg.mapSize = metadata.mapSize;
        cfg.mapType = static_cast<MapType>(metadata.mapType);
        for (const int tribeId : metadata.tribes) {
            cfg.tribes.push_back(static_cast<TribeType>(tribeId));
        }
        Game reconstructed;
        reconstructed.newGame(cfg);
        std::vector<Game> frames;
        frames.reserve(actionIds.size() + 1);
        frames.push_back(reconstructed);

        for (size_t index = 0; index < actionIds.size(); ++index) {
            GameStateAdapter adapter(reconstructed);
            const PlayerId pid = adapter.currentPlayer();
            const std::optional<Action> action = adapter.decodeActionId(pid, actionIds[index]);
            if (!action) throw std::runtime_error("invalid action id at move " + std::to_string(index));
            const std::vector<uint8_t> legal = adapter.legalActionMask(pid);
            if (actionIds[index] >= legal.size() || !legal[actionIds[index]]) {
                throw std::runtime_error("illegal action at move " + std::to_string(index));
            }
            adapter.apply(*action);
            reconstructed = adapter.getGame();
            frames.push_back(reconstructed);
        }

        replayFrames_ = std::move(frames);
        replayMove_ = 0;
        game = replayFrames_.front();
        replayRecorder_.replace(actionIds);
        currentGameConfig_ = cfg;
        autoRandomEnabled = false;
        replayAutoPlayEnabled_ = false;
        playerIsBot_.clear();
        modelClient_.reset();
        configureRenderer(true);
        mode = Mode::ReplayViewer;
        fileStatus_ = "Loaded " + path;
        return true;
    } catch (const std::exception& e) {
        fileStatus_ = std::string("Load failed: ") + e.what();
        return false;
    }
}

bool GuiApp::advanceReplayMove() {
    if (replayMove_ + 1 >= replayFrames_.size()) return false;
    seekReplayMove(replayMove_ + 1);
    return true;
}

void GuiApp::seekReplayMove(size_t move) {
    if (replayFrames_.empty()) return;
    replayMove_ = std::min(move, replayFrames_.size() - 1);
    game = replayFrames_[replayMove_];
    if (mapRenderer) {
        mapRenderer->clearSelection();
        mapRenderer->notifyGameStateChanged();
        mapRenderer->setReplayProgress(replayMove_, replayFrames_.size() - 1);
    }
}

bool GuiApp::handleFileUiEvent(const sf::Event& ev) {
    layoutFileUi();
    if (fileDialogMode_ != FileDialogMode::None) {
        if (ev.type == sf::Event::TextEntered) {
            if (ev.text.unicode >= 32 && ev.text.unicode < 127) {
                filePathBuffer_.push_back(static_cast<char>(ev.text.unicode));
            } else if (ev.text.unicode == 8 && !filePathBuffer_.empty()) {
                filePathBuffer_.pop_back();
            }
            return true;
        }
        if (ev.type == sf::Event::KeyPressed && ev.key.code == sf::Keyboard::Return) {
            const bool load = fileDialogMode_ == FileDialogMode::LoadReplay;
            if (load) loadReplayFile(filePathBuffer_); else saveReplayFile(filePathBuffer_);
            fileDialogMode_ = FileDialogMode::None;
            return true;
        }
        if (ev.type == sf::Event::KeyPressed && ev.key.code == sf::Keyboard::Escape) {
            fileDialogMode_ = FileDialogMode::None;
            return true;
        }
        if (ev.type == sf::Event::MouseButtonPressed && ev.mouseButton.button == sf::Mouse::Left) {
            const sf::Vector2f p(float(ev.mouseButton.x), float(ev.mouseButton.y));
            const sf::FloatRect confirm(710.f, 414.f, 100.f, 32.f);
            const sf::FloatRect cancel(820.f, 414.f, 100.f, 32.f);
            if (confirm.contains(p)) {
                const bool load = fileDialogMode_ == FileDialogMode::LoadReplay;
                if (load) loadReplayFile(filePathBuffer_); else saveReplayFile(filePathBuffer_);
                fileDialogMode_ = FileDialogMode::None;
            } else if (cancel.contains(p)) {
                fileDialogMode_ = FileDialogMode::None;
            }
            return true;
        }
        return true;
    }

    if (ev.type != sf::Event::MouseButtonPressed || ev.mouseButton.button != sf::Mouse::Left) return false;
    const sf::Vector2f p(float(ev.mouseButton.x), float(ev.mouseButton.y));
    if (fileMenuRect_.contains(p)) {
        fileMenuOpen_ = !fileMenuOpen_;
        return true;
    }
    if (fileMenuOpen_ && fileLoadRect_.contains(p)) {
        fileMenuOpen_ = false;
        if (const auto path = openReplayFileDialog()) {
            loadReplayFile(*path);
        } else {
            fileStatus_ = "Load cancelled";
        }
        return true;
    }
    if (fileMenuOpen_ && mode == Mode::InGame && fileSaveRect_.contains(p)) {
        fileMenuOpen_ = false;
        if (const auto path = saveReplayFileDialog()) {
            saveReplayFile(*path);
        } else {
            fileStatus_ = "Save cancelled";
        }
        return true;
    }
    fileMenuOpen_ = false;
    return false;
}

void GuiApp::drawFileUi() {
    layoutFileUi();
    auto drawButton = [&](const sf::FloatRect& rect, const std::string& label) {
        sf::RectangleShape shape({rect.width, rect.height});
        shape.setPosition({rect.left, rect.top});
        shape.setFillColor(sf::Color(35, 35, 35, 245));
        shape.setOutlineThickness(1.f);
        shape.setOutlineColor(sf::Color(110, 110, 110));
        window.draw(shape);
        sf::Text text(label, font, 15);
        text.setFillColor(sf::Color(240, 240, 240));
        text.setPosition(rect.left + 9.f, rect.top + 5.f);
        window.draw(text);
    };

    drawButton(fileMenuRect_, "File");
    if (fileMenuOpen_) {
        drawButton(fileLoadRect_, "Load .polygame");
        if (mode == Mode::InGame) drawButton(fileSaveRect_, "Save .polygame");
    }

    if (!fileStatus_.empty() && fileDialogMode_ == FileDialogMode::None) {
        sf::Text status(fileStatus_, font, 14);
        status.setFillColor(sf::Color(220, 220, 220));
        status.setPosition(std::max(8.f, fileMenuRect_.left - 360.f), 11.f);
        window.draw(status);
    }

    if (fileDialogMode_ == FileDialogMode::None) return;
    sf::RectangleShape shade({1280.f, 720.f});
    shade.setFillColor(sf::Color(0, 0, 0, 150));
    window.draw(shade);
    const sf::FloatRect panel(350.f, 285.f, 580.f, 180.f);
    sf::RectangleShape box({panel.width, panel.height});
    box.setPosition({panel.left, panel.top});
    box.setFillColor(sf::Color(28, 28, 28));
    box.setOutlineThickness(2.f);
    box.setOutlineColor(sf::Color(110, 110, 110));
    window.draw(box);

    const bool loading = fileDialogMode_ == FileDialogMode::LoadReplay;
    sf::Text title(loading ? "Load .polygame replay" : "Save .polygame replay", font, 20);
    title.setFillColor(sf::Color(245, 245, 245));
    title.setPosition(375.f, 310.f);
    window.draw(title);
    const sf::FloatRect input(375.f, 355.f, 530.f, 34.f);
    sf::RectangleShape inputBox({input.width, input.height});
    inputBox.setPosition({input.left, input.top});
    inputBox.setFillColor(sf::Color(18, 18, 18));
    inputBox.setOutlineThickness(1.f);
    inputBox.setOutlineColor(sf::Color(130, 130, 130));
    window.draw(inputBox);
    sf::Text path(filePathBuffer_, font, 16);
    path.setFillColor(sf::Color::White);
    path.setPosition(383.f, 362.f);
    window.draw(path);
    drawButton({710.f, 414.f, 100.f, 32.f}, loading ? "Load" : "Save");
    drawButton({820.f, 414.f, 100.f, 32.f}, "Cancel");
}

void GuiApp::layoutFileUi() {
    const float windowWidth = static_cast<float>(window.getSize().x);
    if (mode == Mode::SelectTribes) {
        // Start screen: after map type selection and before the bot/player
        // configuration area.
        const float x = std::min(840.f, std::max(8.f, windowWidth - 248.f));
        fileMenuRect_ = sf::FloatRect(x, 210.f, 170.f, 36.f);
        fileLoadRect_ = sf::FloatRect(x, 248.f, 230.f, 30.f);
        fileSaveRect_ = sf::FloatRect(x, 278.f, 230.f, 30.f);
        return;
    }

    // Game/replay: use exactly the former rewards-panel column.  MapRenderer
    // reserves 400 px for the right info panel, then 150 px + 12 px padding
    // for this column.
    const float x = std::max(8.f, windowWidth - 562.f);
    constexpr float width = 160.f;
    fileMenuRect_ = sf::FloatRect(x, 8.f, width, 32.f);
    fileLoadRect_ = sf::FloatRect(x, 42.f, width, 30.f);
    fileSaveRect_ = sf::FloatRect(x, 72.f, width, 30.f);
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
    recordActionId(chosen);
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
    recordActionId(static_cast<size_t>(actionId));
    if (mapRenderer) mapRenderer->clearSelection();
    return true;
    // 4. Loop: called again in 200ms with the new state
}
