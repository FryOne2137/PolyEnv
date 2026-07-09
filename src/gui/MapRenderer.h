//
// Created by Fryderyk Niedzwiecki on 16/01/2026.
//

#ifndef GAME_ENGINE_MAPRENDERER_H
#define GAME_ENGINE_MAPRENDERER_H


#pragma once

#include <SFML/Graphics.hpp>   // sf::Sprite, sf::RenderTarget
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/System/Vector2.hpp>
#include <string>
#include <vector>

#include "TextureStore.h"

#include "ai/Action.h"
#include "../game/Game.h"
#include "world/Map.h"
#include "world/Pos.h"
#include "Tile.h"
#include "../content/tribes/Tribe.h"
#include "terrain/BaseTerrainEnum.h"
#include "terrain/ResourcesEnum.h"
#include "terrain/SettlementTypeEnum.h"

class MapRenderer {
public:
    bool hasSelection() const;
    Pos getSelectedPos() const;
    void clearSelection();
    void handleEvent(const sf::Event& ev);
    explicit MapRenderer(TextureStore& store);

    void setGame(Game* g);

    void setTileSizePx(int px);
    void setOrigin(sf::Vector2f o);

    void draw(sf::RenderTarget& rt);
    void setZoom(float z);
    float getZoom() const;

    // --- UI actions (polled by GuiApp) ---
    bool consumeEndTurnClicked();
    bool consumeToggleOverviewRequested();
    bool consumeAutoPlayToggleRequested();
    void toggleOverview();
    void setAutoPlayActive(bool active);
    void notifyGameStateChanged();

private:
    sf::Vector2u lastRtSize{0u, 0u};

    sf::Vector2f computeCenterShift(float z, const sf::Vector2u& rtSz);

    float zoom = 1.0f;

    // Drag-to-pan state (screen space)
    bool dragging = false;
    sf::Vector2f dragLast{0.f, 0.f};

    TextureStore& tex;
    Game* game = nullptr;

    int tilePx = 48;
    sf::Vector2f origin{24.f, 24.f};

    // Path helpers (tries multiple variants)
    std::string baseDir() const { return "assets/textures/"; }

    std::vector<std::string> tribeFolderCandidates(TribeType t) const;
    std::vector<std::string> tileCandidates(TribeType tribe, const std::string& kind) const;
    std::vector<std::string> globalCandidates(const std::string& name) const;

    const sf::Texture& pickFirstExisting(const std::vector<std::string>& paths);

    void drawSprite(sf::RenderTarget& rt, const sf::Texture& tex, float x, float y, float size);

    // Tile selection
    bool selectedValid = false;
    Pos selectedPos{0, 0};

    // Click vs drag discrimination
    sf::Vector2f dragStart{0.f, 0.f};
    bool dragMoved = false;

    // --- UI state ---
    bool showOverview = false;
    bool endTurnClicked = false;
    bool toggleOverviewRequested = false;
    bool autoPlayToggleRequested = false;
    bool autoPlayActive = false;

    bool contextActionCacheValid = false;
    Game* contextActionCacheGame = nullptr;
    bool contextActionCacheSelectedValid = false;
    Pos contextActionCacheSelectedPos{0, 0};
    PlayerId contextActionCachePlayer = kNoPlayer;
    uint32_t contextActionCacheTurn = 0;
    bool contextActionCacheOverview = false;
    std::vector<Action> contextActionCache;

    // Cached clickable rects (screen coords)
    sf::FloatRect btnEndTurn{};
    sf::FloatRect btnAutoPlay{};
    sf::FloatRect btnOverview{};
    sf::FloatRect btnBack{};

    bool pointInTileDiamond(const sf::Vector2f& p, float x, float y, float tileSize, float yStep) const;
    bool screenToTile(const sf::Vector2f& screen, Pos& out);
    mutable sf::Font uiFont;
    mutable bool uiFontLoaded = false;

    void invalidateContextActionCache();
    const std::vector<Action>& contextActionsForCurrentSelection();
    bool ensureUIFontLoaded() const;
    static const char* tribeDisplayName(TribeType t);
    static const char* baseTerrainName(BaseTerrainEnum t);
    static const char* settlementName(SettlementTypeEnum t);
};

#endif //GAME_ENGINE_MAPRENDERER_H
