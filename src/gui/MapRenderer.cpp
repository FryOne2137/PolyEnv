//
// Created by Fryderyk Niedzwiecki on 16/01/2026.
//

#include "MapRenderer.h"
#include "Systems/TurnSystem.h"
#include "Systems/TechSystem.h"
#include "Systems/MovementSystem.h"
#include "Systems/InteractionSystem.h"
#include "Systems/VisionSystem.h"
#include "terrain/VisibilityEnum.h"
#include "Game.h"
#include "tech/TechDB.h"
#include <SFML/Window/Mouse.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <array>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <limits>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/CircleShape.hpp>

#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Font.hpp>

// --- simple perf logger (GUI + game actions) ---
struct ScopedPerfLog {
    const char* name;
    std::chrono::high_resolution_clock::time_point t0;
    explicit ScopedPerfLog(const char* n)
        : name(n), t0(std::chrono::high_resolution_clock::now()) {}
    ~ScopedPerfLog() {
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        std::cout << "[Perf] " << name << " took " << us << " us" << std::endl;
    }
};

static inline void perfLog(const char* name, const std::function<void()>& fn) {
    ScopedPerfLog _pl(name);
    fn();
}

static inline bool perfLogBool(const char* name, const std::function<bool()>& fn) {
    ScopedPerfLog _pl(name);
    return fn();
}

static inline void perfLogIf(const char* name, bool cond, const std::function<void()>& fn) {
    if (!cond) { fn(); return; }
    ScopedPerfLog _pl(name);
    fn();
}



static bool existsFile(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(p), ec);
}

static std::string firstExistingWithParents(const std::vector<std::string>& paths) {
    // CLion often runs the executable from cmake-build-*/ so relative paths like "assets/..." won't work.
    // We try a few parent prefixes to reach the project root.
    static const std::array<std::string, 6> kPrefixes = {"", "../", "../../", "../../../", "../../../../", "../../../../../"};

    for (const auto& p : paths) {
        for (const auto& pre : kPrefixes) {
            const std::string candidate = pre + p;
            if (existsFile(candidate)) return candidate;
        }
    }
    return {};
}

static const sf::Texture& fallbackMissingTexture() {
    static sf::Texture tex;
    static bool inited = false;
    if (!inited) {
        sf::Image img;
        const unsigned sz = 64;
        img.create(sz, sz);
        for (unsigned y = 0; y < sz; ++y) {
            for (unsigned x = 0; x < sz; ++x) {
                const bool a = ((x / 8) % 2) == ((y / 8) % 2);
                img.setPixel(x, y, a ? sf::Color(255, 0, 255) : sf::Color(0, 0, 0));
            }
        }
        tex.loadFromImage(img);
        inited = true;
    }
    return tex;
}

// Helper: resolve a City object for a given tribe (best-effort: find player by tribe, then use their first owned city id).

static const City* resolveCityForTribe(const Game* game, TribeType tribe) {
    if (!game) return nullptr;
    const auto& pls = game->getPlayers();
    for (const Player& pl : pls) {
        if (pl.getTribeType() != tribe) continue;
        const auto& owned = pl.getCities();
        if (owned.empty()) return nullptr;
        return game->getCity(owned.front());
    }
    return nullptr;
}

// Resolve city object using the tile's settlement id (works for enemy cities too).
static const City* resolveCityForTile(const Game* game, const Tile& tile) {
    if (!game) return nullptr;
    if (tile.getSettlementType() != SettlementTypeEnum::City) return nullptr;
    return game->getCityBySettlementId(tile.getSettlementId());
}

static void logMissingOnce(const std::vector<std::string>& paths) {
    static std::unordered_set<std::string> logged;

    const std::string logical = paths.empty() ? std::string("<empty>") : paths.front();

    if (!logged.insert(logical).second) return;

    std::cerr << "[TextureStore] Missing texture (logical): " << logical << "\n";
    std::cerr << "  Tried (with parent prefixes):\n";

    static const std::array<std::string, 6> kPrefixes = {"", "../", "../../", "../../../", "../../../../", "../../../../../"};

    // Print only first few to keep logs readable.
    int printed = 0;
    const int kMaxPrinted = 24;
    for (const auto& p : paths) {
        for (const auto& pre : kPrefixes) {
            if (printed >= kMaxPrinted) break;
            std::cerr << "    - " << (pre + p) << "\n";
            ++printed;
        }
        if (printed >= kMaxPrinted) break;
    }
    if (printed >= kMaxPrinted) {
        std::cerr << "    ... (more omitted)\n";
    }
}


// --- Tech tree click hitboxes (captured from last draw) ---
static bool g_techHitValid = false;
static sf::FloatRect g_techHitArea;
static float g_techHitRadius = 0.f;
static std::unordered_map<TechId, sf::Vector2f> g_techHitPos;

// --- Movement click/overlay state (gameplay view only) ---
static UnitId g_moveSelectedUnit = kNoUnit;
static bool g_moveOverlayValid = false;
static std::unordered_set<uint32_t> g_moveOverlaySet; // key: (y<<16)|x

// --- Attack click/overlay state (gameplay view only) ---
static bool g_attackOverlayValid = false;
static std::unordered_set<uint32_t> g_attackOverlaySet; // key: (y<<16)|x

static constexpr float kLeftSpawnPanelW = 150.f;

// --- Spawn panel (left side) ---
static bool g_spawnHitValid = false;
static UnitType g_spawnSelectedType = UnitType::Unknown;

struct SpawnIconHit {
    sf::FloatRect rect;
    UnitType type;
};
static std::vector<SpawnIconHit> g_spawnHits;

// --- Context action button state (bottom bar under the map) ---
enum class ActionKind {
    None,
    CaptureVillage,
    CaptureCity,
    TrainUnit,
    BuildBuilding,
    ClearForest,
    Hunt,
    Fishing,
    BurnForest,
    GrowForest,
    DestroyTile,
    Organization,
    UpgradeRaftToScout,
    UpgradeRaftToRammer,
    UpgradeRaftToBomber,
    BecomeVeteran,
    BuildRoad,
    BuildBridge,
    Explorer,
    ExploreRuin,
    CollectStarfish,
};
struct ActionButton {
    sf::FloatRect rect;
    ActionKind kind;
    UnitType trainType = UnitType::Unknown;
    BuildingTypeEnum buildType = BuildingTypeEnum::None;
};

static bool g_actionBtnsValid = false;
static std::array<ActionButton, 48> g_actionBtns;
static int g_actionBtnCount = 0;
static Pos g_actionPos{0, 0};

static inline uint32_t posKey(Pos p) {
    return (uint32_t(p.y) << 16u) | uint32_t(p.x);
}

static void clearMoveOverlay() {
    g_moveOverlayValid = false;
    g_moveOverlaySet.clear();
}

static void clearAttackOverlay() {
    g_attackOverlayValid = false;
    g_attackOverlaySet.clear();
}

static void clearUnitOverlays() {
    clearMoveOverlay();
    clearAttackOverlay();
}

static void rebuildMoveOverlay(const Game* game, UnitId uid) {
    clearMoveOverlay();
    if (!game) return;

    const Unit* u = game->getUnit(uid);
    if (!u) return;

    // Only current player's units.
    if (u->getOwnerId() != game->getCurrentPlayerId()) return;

    std::vector<Pos> tiles;
    {
        const auto t0 = std::chrono::high_resolution_clock::now();
        tiles = game->reachable(uid);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        std::cout << "[Perf] reachable took " << us << " us" << std::endl;
    }
    g_moveOverlaySet.reserve(tiles.size() * 2u + 1u);
    for (const Pos& p : tiles) {
        g_moveOverlaySet.insert(posKey(p));
    }
    g_moveOverlayValid = true; // valid even if empty
}

static void rebuildAttackOverlay(const Game* game, UnitId uid) {
    clearAttackOverlay();
    if (!game) return;

    const Unit* u = game->getUnit(uid);
    if (!u) return;

    // Only current player's units.
    if (u->getOwnerId() != game->getCurrentPlayerId()) return;

    const std::vector<Pos> tiles = game->attackable(uid);
    g_attackOverlaySet.reserve(tiles.size() * 2u + 1u);
    for (const Pos& p : tiles) {
        g_attackOverlaySet.insert(posKey(p));
    }
    g_attackOverlayValid = true; // valid even if empty
}

// Fixed Polytopia-like tech layout (hand-tuned to match the screenshot).
// Coordinates are in arbitrary "layout units" around the center (0,0). We scale them to fit.
struct TechNormPos { float x; float y; };

static void buildFixedTechTreeLayout(const std::vector<TechId>& techs,
                                    std::unordered_map<TechId, sf::Vector2f>& pos,
                                    const sf::Vector2f& center,
                                    float w,
                                    float h)
{
    static const std::unordered_map<TechId, TechNormPos> kNorm = {
        // Top branch
        {TechId::Riding,       { 0.00f, -1.05f}},
        {TechId::Roads,        { 0.00f, -2.10f}},
        {TechId::Trade,        { 0.00f, -3.15f}},
        {TechId::FreeSpirit,   { 1.20f, -1.55f}},
        {TechId::Chivalry,     { 2.55f, -2.55f}},

        // Right branch
        {TechId::Organization, { 1.55f,  0.20f}},
        {TechId::Farming,      { 2.70f, -0.55f}},
        {TechId::Construction, { 3.85f, -1.10f}},
        {TechId::Strategy,     { 2.35f,  0.95f}},
        {TechId::Diplomacy,    { 3.55f,  1.65f}},

        // Bottom branch
        {TechId::Climbing,     { 0.20f,  1.25f}},
        {TechId::Mining,       { 1.35f,  2.05f}},
        {TechId::Smithery,     { 2.55f,  2.95f}},
        {TechId::Meditation,   {-0.55f,  2.55f}},
        {TechId::Philosophy,   {-1.05f,  3.65f}},

        // Left-bottom branch (Aquatism line)
        {TechId::Fishing,      {-1.35f,  0.55f}},
        {TechId::Ramming,      {-2.60f,  0.55f}},
        {TechId::Aquatism,     {-3.75f,  0.55f}},
        {TechId::Sailing,      {-1.75f,  1.65f}},
        {TechId::Navigation,   {-2.15f,  2.75f}},

        // Left-top branch
        {TechId::Hunting,      {-1.15f, -0.70f}},
        {TechId::Forestry,     {-1.65f, -1.85f}},
        {TechId::Mathematics,  {-2.10f, -2.95f}},
        {TechId::Archery,      {-2.35f, -0.65f}},
        {TechId::Spiritualism, {-3.60f, -0.60f}},
    };

    // Compute bounds of known techs
    float minX =  std::numeric_limits<float>::max();
    float minY =  std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();

    std::vector<TechId> unknown;
    unknown.reserve(techs.size());

    int knownCount = 0;
    for (TechId t : techs) {
        auto it = kNorm.find(t);
        if (it == kNorm.end()) { unknown.push_back(t); continue; }
        ++knownCount;
        minX = std::min(minX, it->second.x);
        minY = std::min(minY, it->second.y);
        maxX = std::max(maxX, it->second.x);
        maxY = std::max(maxY, it->second.y);
    }

    if (knownCount == 0) {
        minX = -1.f; maxX = 1.f;
        minY = -1.f; maxY = 1.f;
    }

    // Fit into available rect with padding
    const float pad = 0.06f;
    const float availW = std::max(10.f, w * (1.f - 2.f * pad));
    const float availH = std::max(10.f, h * (1.f - 2.f * pad));

    const float spanX = std::max(1e-3f, maxX - minX);
    const float spanY = std::max(1e-3f, maxY - minY);
    const float scale = std::min(availW / spanX, availH / spanY);

    pos.clear();
    pos.reserve(techs.size() * 2 + 1);

    for (TechId t : techs) {
        auto it = kNorm.find(t);
        if (it == kNorm.end()) continue;
        const TechNormPos np = it->second;
        pos[t] = {center.x + np.x * scale, center.y + np.y * scale};
    }

    // Unknown techs: place them on an outer ring so they remain clickable/visible.
    if (!unknown.empty()) {
        constexpr float PI = 3.14159265f;
        const float ringR = std::min(availW, availH) * 0.50f;
        for (size_t i = 0; i < unknown.size(); ++i) {
            const float a = -PI * 0.5f + (2.f * PI) * (float(i) / float(unknown.size()));
            pos[unknown[i]] = {center.x + std::cos(a) * ringR, center.y + std::sin(a) * ringR};
        }
    }
}

MapRenderer::MapRenderer(TextureStore& store) : tex(store) {}

void MapRenderer::setGame(Game* g) { game = g; }

void MapRenderer::setTileSizePx(int px) { tilePx = px; }
void MapRenderer::setOrigin(sf::Vector2f o) { origin = o; }

void MapRenderer::setZoom(float z) {
    // Reasonable clamps
    if (z < 0.35f) z = 0.35f;
    if (z > 3.0f)  z = 3.0f;
    zoom = z;
}

static void drawTileDiamondOverlay(sf::RenderTarget& rt,
                                  float x,
                                  float y,
                                  float tileSize,
                                  float yStep,
                                  const sf::Color& c)
{
    const sf::Vector2f top  {x + tileSize * 0.5f, y};
    const sf::Vector2f right{x + tileSize,        y + yStep};
    const sf::Vector2f bot  {x + tileSize * 0.5f, y + 2.f * yStep};
    const sf::Vector2f left {x,                   y + yStep};

    sf::VertexArray fill(sf::TriangleFan, 4);
    fill[0].position = top;
    fill[1].position = right;
    fill[2].position = bot;
    fill[3].position = left;
    for (std::size_t i = 0; i < 4; ++i) fill[i].color = c;
    rt.draw(fill);
}

float MapRenderer::getZoom() const { return zoom; }

bool MapRenderer::hasSelection() const { return selectedValid; }
Pos MapRenderer::getSelectedPos() const { return selectedPos; }
void MapRenderer::clearSelection() { selectedValid = false; }

bool MapRenderer::consumeEndTurnClicked() {
    const bool v = endTurnClicked;
    endTurnClicked = false;
    return v;
}

bool MapRenderer::consumeToggleOverviewRequested() {
    const bool v = toggleOverviewRequested;
    toggleOverviewRequested = false;
    return v;
}

void MapRenderer::toggleOverview() {
    showOverview = !showOverview;

    // Clear movement and attack selection/overlay when switching views.
    g_moveSelectedUnit = kNoUnit;
    clearUnitOverlays();
}

void MapRenderer::handleEvent(const sf::Event& ev) {
    // Toggle universal view
    if (ev.type == sf::Event::KeyPressed && ev.key.code == sf::Keyboard::Tab) {
        toggleOverviewRequested = true;
        return;
    }
    // --- Pan: LEFT mouse drag ---
    // --- Pan (drag) + Select (click): LEFT mouse ---
    if (ev.type == sf::Event::MouseButtonPressed) {
        if (ev.mouseButton.button == sf::Mouse::Left) {
            dragging = true;
            dragMoved = false;
            dragStart = sf::Vector2f(float(ev.mouseButton.x), float(ev.mouseButton.y));
            dragLast  = dragStart;
        }
    } else if (ev.type == sf::Event::MouseButtonReleased) {
        if (ev.mouseButton.button == sf::Mouse::Left) {
            dragging = false;

            // Treat as click if mouse didn't move much
            const sf::Vector2f up(float(ev.mouseButton.x), float(ev.mouseButton.y));
            const sf::Vector2f d = up - dragStart;
            const float dist2 = d.x * d.x + d.y * d.y;

            if (!dragMoved && dist2 < 25.f) {

                // --- Left spawn panel click (gameplay view only) ---
                // --- Left spawn panel click (gameplay view only) ---
                // Klik ikonki = wybór + natychmiastowa próba spawnu na zaznaczonym tile.
                if (!showOverview && game && up.x < kLeftSpawnPanelW && g_spawnHitValid) {
                    for (const SpawnIconHit& h : g_spawnHits) {
                        if (!h.rect.contains(up)) continue;

                        g_spawnSelectedType = h.type; // zostaw zaznaczone do kolejnych spawnów

                        // Spawn na aktualnie zaznaczonym tile (bez walidacji w GUI)
                        if (selectedValid && game->getMap().inBounds(selectedPos)) {
                            perfLog("SpawnUnit", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const UnitId uid = game->spawnUnit(g_spawnSelectedType, pid, selectedPos, /*canActImmediately=*/false);
                                (void)uid;
                            });
                        }
                        return; // consume
                    }
                    return; // klik w tło panelu
                }

                // --- Bottom action button (e.g. capture/train/build) ---
                if (!showOverview && game && g_actionBtnCount > 0) {
                    for (int bi = 0; bi < g_actionBtnCount; ++bi) {
                        const ActionButton& ab = g_actionBtns[bi];
                        if (!ab.rect.contains(up)) continue;
                        if (ab.kind == ActionKind::CaptureVillage || ab.kind == ActionKind::CaptureCity) {
                            // For capture actions we require a unit on the selected tile.
                            const UnitId uOn = game->getMap().unitOn(g_actionPos);
                            if (uOn != Map::kNoUnit) {
                                if (ab.kind == ActionKind::CaptureVillage) {
                                    perfLog("CaptureVillage", [&] {
                                        InteractionSystem::handleVillage(*game, uOn, g_actionPos);
                                    });
                                } else {
                                    perfLog("CaptureCity", [&] {
                                        InteractionSystem::handleCityCapture(*game, uOn, g_actionPos);
                                    });
                                }
                            }
                        } else if (ab.kind == ActionKind::TrainUnit) {
                            perfLog("TrainUnit", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const UnitId uid = game->spawnUnit(ab.trainType, pid, g_actionPos, /*canActImmediately=*/false);
                                (void)uid;
                            });
                        } else if (ab.kind == ActionKind::BuildBuilding) {
                            perfLog("BuildBuilding", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->buildBuilding(pid, g_actionPos, ab.buildType);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::ClearForest) {
                            perfLog("ClearForest", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->clearForest(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::BurnForest) {
                            perfLog("BurnForest", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->burnForest(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::GrowForest) {
                            perfLog("GrowForest", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->growForest(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::Hunt) {
                            perfLog("Hunt", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->hunt(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::Fishing) {
                            perfLog("Fishing", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->fishing(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::DestroyTile) {
                            perfLog("DestroyTile", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->destroyTile(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::Organization) {
                            perfLog("Organization", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->organization(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::UpgradeRaftToScout) {
                            perfLog("UpgradeRaftToScout", [&] {
                                const UnitId uid = game->getMap().unitOn(g_actionPos);
                                if (uid != Map::kNoUnit) {
                                    const bool ok = game->upgradeRaftToScout(uid);
                                    (void)ok;
                                }
                            });
                        } else if (ab.kind == ActionKind::UpgradeRaftToRammer) {
                            perfLog("UpgradeRaftToRammer", [&] {
                                const UnitId uid = game->getMap().unitOn(g_actionPos);
                                if (uid != Map::kNoUnit) {
                                    const bool ok = game->upgradeRaftToRammer(uid);
                                    (void)ok;
                                }
                            });
                        } else if (ab.kind == ActionKind::UpgradeRaftToBomber) {
                            perfLog("UpgradeRaftToBomber", [&] {
                                const UnitId uid = game->getMap().unitOn(g_actionPos);
                                if (uid != Map::kNoUnit) {
                                    const bool ok = game->upgradeRaftToBomber(uid);
                                    (void)ok;
                                }
                            });
                        } else if (ab.kind == ActionKind::BecomeVeteran) {
                            perfLog("BecomeVeteran", [&] {
                                const UnitId uid = game->getMap().unitOn(g_actionPos);
                                if (uid != Map::kNoUnit) {
                                    const bool ok = game->becomeVeteran(uid);
                                    (void)ok;
                                }
                            });
                        } else if (ab.kind == ActionKind::BuildRoad) {
                            perfLog("BuildRoad", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->buildRoad(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::BuildBridge) {
                            perfLog("BuildBridge", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->buildBridge(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::Explorer) {
                            perfLog("Explorer", [&] {
                                const PlayerId pid = game->getCurrentPlayerId();
                                const bool ok = game->explorer(pid, g_actionPos);
                                (void)ok;
                            });
                        } else if (ab.kind == ActionKind::ExploreRuin) {
                            perfLog("ExploreRuin", [&] {
                                const UnitId uid = game->getMap().unitOn(g_actionPos);
                                if (uid != Map::kNoUnit) {
                                    const bool ok = game->handleRuin(uid, g_actionPos);
                                    (void)ok;
                                }
                            });
                        } else if (ab.kind == ActionKind::CollectStarfish) {
                            perfLog("CollectStarfish", [&] {
                                const UnitId uid = game->getMap().unitOn(g_actionPos);
                                if (uid != Map::kNoUnit) {
                                    const bool ok = game->handleStarfish(uid, g_actionPos);
                                    (void)ok;
                                }
                            });
                        }
                        // Clear overlays after action.
                        g_moveSelectedUnit = kNoUnit;
                        clearUnitOverlays();
                        selectedValid = false;
                        g_actionBtnsValid = false;
                        g_actionBtnCount = 0;
                        return; // consume click
                    }
                }
                // --- UI buttons (right panel) ---
                // --- Tech tree click (buy tech) ---
                // Tech nodes are rendered in the right panel during gameplay view.
                // If the user clicks a node, try to purchase it for the current player.
                if (!showOverview && game && g_techHitValid && g_techHitArea.contains(up)) {
                    TechId hit = TechId::Count;
                    const float r2 = g_techHitRadius * g_techHitRadius;
                    for (const auto& kv : g_techHitPos) {
                        const sf::Vector2f p = kv.second;
                        const float dx = up.x - p.x;
                        const float dy = up.y - p.y;
                        if (dx * dx + dy * dy <= r2) {
                            hit = kv.first;
                            break;
                        }
                    }

                    if (hit != TechId::Count) {
                        perfLog("BuyTechClick", [&] {
                            auto curPid = game->getCurrentPlayerId();
                            Player& curPl = game->getPlayer(curPid);
                            if (TechSystem::canBuyTech(curPl, hit)) {
                                TechSystem::buyTech(curPl, hit);
                            }
                        });
                        return; // consume click (avoid selecting a tile)
                    }
                }

                if (!showOverview) {
                    if (btnEndTurn.contains(up)) {
                        endTurnClicked = true;
                        return;
                    }
                    if (btnOverview.contains(up)) {
                        toggleOverviewRequested = true;
                        return;
                    }
                } else {
                    if (btnBack.contains(up)) {
                        toggleOverviewRequested = true;
                        return;
                    }
                }

                // --- Tile selection / movement (gameplay view) ---
                Pos hit;
                if (screenToTile(up, hit)) {
                    selectedPos = hit;
                    selectedValid = true;

                    // Spawn mode: if unit selected, attempt to spawn on clicked tile.
                    // No GUI validation: Game/Systems decide if allowed.
                    // if (!showOverview && game && g_spawnSelectedType != UnitType::Unknown) {
                    //     perfLog("SpawnUnit", [&] {
                    //         const PlayerId pid = game->getCurrentPlayerId();
                    //         const UnitId uid = game->spawnUnit(g_spawnSelectedType, pid, hit, /*canActImmediately=*/false);
                    //         (void)uid;
                    //     });
                    //     g_moveSelectedUnit = kNoUnit;
                    //     clearUnitOverlays();
                    //     return;
                    // }

                    // NOTE: capture (hint click) should work in both gameplay and Map View.
                    // We still disable movement overlay in Map View, but allow capture clicks.
                    if (showOverview) {
                        g_moveSelectedUnit = kNoUnit;
                        clearUnitOverlays();
                    }

                    if (game) {
                        // In Map View, stop here (capture clicks already handled above).
                        if (showOverview) return;
                        const Map& m = game->getMap();
                        const UnitId onTile = m.unitOn(hit);

                        // 1) Click on current player's unit => show reachable tiles
                        if (onTile != Map::kNoUnit) {
                            const Unit* uu = game->getUnit(onTile);
                            if (uu && uu->getOwnerId() == game->getCurrentPlayerId()) {
                                // Toggle off if same unit clicked again
                                if (g_moveSelectedUnit == onTile && g_moveOverlayValid) {
                                    g_moveSelectedUnit = kNoUnit;
                                    clearUnitOverlays();
                                } else {
                                    g_moveSelectedUnit = onTile;
                                    rebuildMoveOverlay(game, g_moveSelectedUnit);
                                    rebuildAttackOverlay(game, g_moveSelectedUnit);
                                }
                                return;
                            }
                        }

                        // 2) Click on a highlighted attack tile => attack
                        if (g_moveSelectedUnit != kNoUnit && g_attackOverlayValid) {
                            if (g_attackOverlaySet.find(posKey(hit)) != g_attackOverlaySet.end()) {
                                perfLog("Attack", [&] {
                                    const bool ok = game->attack(g_moveSelectedUnit, hit);
                                    (void)ok;
                                });
                                // After an attack we clear overlays.
                                g_moveSelectedUnit = kNoUnit;
                                clearUnitOverlays();
                                return;
                            }
                        }

                        // 3) Click on a highlighted reachable tile => move
                        if (g_moveSelectedUnit != kNoUnit && g_moveOverlayValid) {
                            if (g_moveOverlaySet.find(posKey(hit)) != g_moveOverlaySet.end()) {
                                perfLog("MoveUnit", [&] {
                                    const bool ok = game->moveUnit(g_moveSelectedUnit, hit);
                                    (void)ok;
                                });
                                // After a move we clear the overlay (simple UX). Re-click unit to move again.
                                g_moveSelectedUnit = kNoUnit;
                                clearUnitOverlays();
                                return;
                            }
                        }

                        // 4) Otherwise clear overlay
                        g_moveSelectedUnit = kNoUnit;
                        clearUnitOverlays();
                    }
                } else {
                    selectedValid = false;
                    g_moveSelectedUnit = kNoUnit;
                    clearUnitOverlays();
                }
            }
        }
    } else if (ev.type == sf::Event::MouseMoved) {
        if (dragging) {
            const sf::Vector2f cur(float(ev.mouseMove.x), float(ev.mouseMove.y));
            if (std::abs(cur.x - dragStart.x) > 3.f || std::abs(cur.y - dragStart.y) > 3.f)
                dragMoved = true;

            const sf::Vector2f d = cur - dragLast;
            origin += d;
            dragLast = cur;
        }
    }
    // --- Zoom / pan by wheel ---
    if (ev.type != sf::Event::MouseWheelScrolled) return;

    const float delta = ev.mouseWheelScroll.delta;
    const sf::Vector2f mouse(float(ev.mouseWheelScroll.x), float(ev.mouseWheelScroll.y));

    const bool shift = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) ||
                       sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);

    // SHIFT + wheel => pan (trackpads may provide horizontal wheel)
    if (shift) {
        const float panStep = 85.f; // pixels per wheel step (trochę większe niż było)
        if (ev.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel) {
            origin.y += -delta * panStep;
        } else if (ev.mouseWheelScroll.wheel == sf::Mouse::HorizontalWheel) {
            origin.x += -delta * panStep;
        }
        return;
    }

    // Wheel => zoom (smooth exponential scale), cursor-anchored even with auto-centering
    // We must account for the auto-centering shift that changes with zoom.
    if (!game || lastRtSize.x == 0u || lastRtSize.y == 0u) {
        // Fallback before first draw: best-effort around cursor
        const float oldZoom = zoom;
        const float factor = std::exp(delta * 0.12f);
        setZoom(zoom * factor);
        const float applied = (oldZoom == 0.f) ? 1.f : (zoom / oldZoom);
        origin = mouse + (origin - mouse) * applied;
        return;
    }

    const float oldZoom = zoom;
    sf::Vector2f cs0 = computeCenterShift(oldZoom, lastRtSize);
    cs0.x += kLeftSpawnPanelW;
    const sf::Vector2f world = mouse - (cs0 + origin);

    const float factor = std::exp(delta * 0.12f);
    setZoom(zoom * factor);

    sf::Vector2f cs1 = computeCenterShift(zoom, lastRtSize);
    cs1.x += kLeftSpawnPanelW;
    origin = mouse - cs1 - world;}

sf::Vector2f MapRenderer::computeCenterShift(float z, const sf::Vector2u& rtSz) {
    sf::Vector2f shift{0.f, 0.f};
    if (!game) return shift;

    const Map& map = game->getMap();


    const float tileSize = float(tilePx) * z;

    const sf::Texture& refGround = pickFirstExisting(tileCandidates(TribeType::XinXi, "ground"));
    const auto refSz = refGround.getSize();
    const float refW = refSz.x ? float(refSz.x) : 1.f;
    const float refH = refSz.y ? float(refSz.y) : 1.f;

    const float scaledTileH = refH * tileSize / refW;
    const float yStep = scaledTileH * (606.f / 1908.f);

    float minX = 1e30f, minY = 1e30f;
    float maxX = -1e30f, maxY = -1e30f;

    for (int r = 0; r < map.getHeight(); ++r) {
        for (int c = 0; c < map.getWidth(); ++c) {
            const float xRaw = -tileSize / 2.f + (float(c - r) * tileSize / 2.f);
            const float yRaw = (float(c + r) * yStep);

            minX = std::min(minX, xRaw);
            minY = std::min(minY, yRaw);
            maxX = std::max(maxX, xRaw + tileSize);
            maxY = std::max(maxY, yRaw + scaledTileH);
        }
    }

    const float contentW = std::max(1.f, maxX - minX);
    const float contentH = std::max(1.f, maxY - minY);

    const float centerShiftX = (float(rtSz.x) - contentW) * 0.5f - minX;
    const float centerShiftY = (float(rtSz.y) - contentH) * 0.5f - minY;

    shift = {centerShiftX, centerShiftY};
    return shift;
}

bool MapRenderer::ensureUIFontLoaded() const {
    if (uiFontLoaded) return true;

    // SFML has no default font. On macOS we load a system font.
    const char* macFonts[] = {
        "/System/Library/Fonts/SFNS.ttf", // San Francisco
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica.ttf"
    };

    for (const char* p : macFonts) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(p), ec)) {
            if (uiFont.loadFromFile(p)) {
                uiFontLoaded = true;
                return true;
            }
        }
    }

    return false;
}


bool MapRenderer::pointInTileDiamond(const sf::Vector2f& p, float x, float y, float tileSize, float yStep) const {
    // The visible isometric cell is a diamond of width = tileSize and height = 2*yStep.
    // (Textures may be taller due to transparent margins.)
    const float cellH = 2.f * yStep;

    const float cx = x + tileSize * 0.5f;
    const float cy = y + cellH * 0.5f;

    const float dx = std::abs(p.x - cx) / (tileSize * 0.5f);
    const float dy = std::abs(p.y - cy) / (cellH * 0.5f);
    return (dx + dy) <= 1.f;
}

bool MapRenderer::screenToTile(const sf::Vector2f& screen, Pos& out) {
    if (!game) return false;
    const Map& map = game->getMap();
    if (lastRtSize.x == 0u || lastRtSize.y == 0u) return false;

    const float tileSize = float(tilePx) * zoom;

    const sf::Texture& refGround = pickFirstExisting(tileCandidates(TribeType::XinXi, "ground"));
    const auto refSz = refGround.getSize();
    const float refW = refSz.x ? float(refSz.x) : 1.f;
    const float refH = refSz.y ? float(refSz.y) : 1.f;
    const float scaledTileH = refH * tileSize / refW;
    const float yStep = scaledTileH * (606.f / 1908.f);

    sf::Vector2f cs = computeCenterShift(zoom, lastRtSize);
    cs.x += kLeftSpawnPanelW;
    const sf::Vector2f baseShift{cs.x + origin.x, cs.y + origin.y};


    for (int row = 0; row < map.getHeight(); ++row) {
        for (int column = 0; column < map.getWidth(); ++column) {
            const float x = baseShift.x - tileSize / 2.f + (float(column - row) * tileSize / 2.f);
            const float y = baseShift.y + (float(column + row) * yStep);

            if (pointInTileDiamond(screen, x, y, tileSize, yStep)) {
                out = Pos{column, row};
                return true;
            }
        }
    }

    return false;
}

const char* MapRenderer::tribeDisplayName(TribeType t) {
    switch (t) {
        case TribeType::XinXi:    return "XinXi";
        case TribeType::Imperius: return "Imperius";
        case TribeType::Bardur:   return "Bardur";
        case TribeType::Oumaji:   return "Oumaji";
        case TribeType::Kickoo:   return "Kickoo";
        case TribeType::Hoodrick: return "Hoodrick";
        case TribeType::Luxidoor: return "Luxidoor";
        case TribeType::Vengir:   return "Vengir";
        case TribeType::Zebasi:   return "Zebasi";
        case TribeType::AiMo:     return "AiMo";
        case TribeType::Quetzali: return "Quetzali";
        case TribeType::Yadakk:   return "Yadakk";
        case TribeType::Aquarion: return "Aquarion";
        case TribeType::Elyrion:  return "Elyrion";
        case TribeType::Polaris:  return "Polaris";
        case TribeType::Cymanti:  return "Cymanti";
        default: return "Unknown";
    }
}

const char* MapRenderer::baseTerrainName(BaseTerrainEnum t) {
    switch (t) {
        case BaseTerrainEnum::Land:     return "Land";
        case BaseTerrainEnum::Mountain: return "Mountain";
        case BaseTerrainEnum::Water:    return "Water";
        case BaseTerrainEnum::Ocean:    return "Ocean";
        default: return "?";
    }
}

const char* MapRenderer::settlementName(SettlementTypeEnum t) {
    switch (t) {
        case SettlementTypeEnum::None:     return "None";
        case SettlementTypeEnum::Village:  return "Village";
        case SettlementTypeEnum::City:     return "City";
        case SettlementTypeEnum::Ruin:     return "Ruin";
        case SettlementTypeEnum::Starfish: return "Starfish";
        default: return "?";
    }
}


static const char* unitTypeToFile(UnitType t) {
    switch (t) {
        // --- Core land ---
        case UnitType::Warrior:    return "warrior";
        case UnitType::Archer:     return "archer";
        case UnitType::Defender:   return "defender";
        case UnitType::Rider:      return "rider";
        case UnitType::MindBender: return "mind_bender";
        case UnitType::Swordsman:  return "swordsman";
        case UnitType::Catapult:   return "catapult";
        case UnitType::Cloak:      return "cloak";
        case UnitType::Knight:     return "knight";
        case UnitType::Giant:      return "giant";
        case UnitType::Bunny:      return "bunny";

        // --- Naval (pack contains both old/new names; we prefer *ship variants) ---
        case UnitType::Raft:       return "transportship";         // boat.png
        case UnitType::Scout:      return "scoutship";    // scoutship.png
        case UnitType::Rammer:     return "rammership";   // rammership.png
        case UnitType::Bomber:     return "bombership";   // bombership.png
        case UnitType::Dinghy:     return "transportship"; // transportship.png
        case UnitType::Pirate:     return "pirate";       // pirate.png
        case UnitType::Juggernaut: return "juggernaut";   // juggernaut.png

        // --- Aquarion ---
        case UnitType::Mermaid:            return "mermaid_warrior";
        case UnitType::AquaticAmphibian:   return "amphibian";
        case UnitType::MermaidArcher:      return "mermaid_archer";
        case UnitType::MermaidDefender:    return "mermaid_defender";
        case UnitType::Swordsmaid:         return "mermaid_swordsman";
        case UnitType::Siren:              return "siren";
        case UnitType::Shark:              return "shark";
        case UnitType::YellyBelly:         return "jelly";
        case UnitType::TridentionAq:       return "tridention";
        case UnitType::CrabAq:             return "crab";

        // --- Elyrion ---
        case UnitType::Polytaur:   return "polytaur";
        case UnitType::DragonEgg:  return "dragon_egg";
        case UnitType::BabyDragon: return "baby_dragon";
        case UnitType::FireDragon: return "fire_dragon";

        // --- Polaris ---
        case UnitType::IceArcher:   return "ice_archer";
        case UnitType::BattleSled:  return "battle_sled";
        case UnitType::Mooni:       return "mooni";
        case UnitType::IceFortress: return "ice_fortress";
        case UnitType::Gaami:       return "gaami";

        // --- Cymanti ---
        case UnitType::Hexapod:      return "hexapod";
        case UnitType::Kiton:        return "kiton";
        case UnitType::Phychi:       return "phychi";
        case UnitType::Shaman:       return "shaman";
        case UnitType::Raychi:       return "raychi";
        case UnitType::Exida:        return "exida";
        case UnitType::Doomux:       return "doomux";
        case UnitType::MothC:        return "moth";
        case UnitType::LarvaC:       return "larva";
        case UnitType::InsectEgg:    return "bug_egg";
        case UnitType::Boomchi:      return "boomchi";
        case UnitType::LivingIsland: return "island";

        // --- Fallbacks ---
        case UnitType::GiantSuper:   return "giant";
        default:                     return "warrior";
    }
}

static const char* unitTypeName(UnitType t) {
    switch (t) {
        case UnitType::Warrior:    return "Warrior";
        case UnitType::Archer:     return "Archer";
        case UnitType::Defender:   return "Defender";
        case UnitType::Rider:      return "Rider";
        case UnitType::MindBender: return "MindBender";
        case UnitType::Swordsman:  return "Swordsman";
        case UnitType::Catapult:   return "Catapult";
        case UnitType::Cloak:      return "Cloak";
        case UnitType::Knight:     return "Knight";
        case UnitType::Giant:      return "Giant";
        case UnitType::Bunny:      return "Bunny";

            // Naval
        case UnitType::Raft:       return "Raft";
        case UnitType::Scout:      return "Scout";
        case UnitType::Rammer:     return "Rammer";
        case UnitType::Bomber:     return "Bomber";
        case UnitType::Dinghy:     return "Dinghy";
        case UnitType::Pirate:     return "Pirate";
        case UnitType::Juggernaut: return "Juggernaut";

        default:                   return "Unit";
    }
}

static std::string unitDisplayString(const Unit& u) {
    std::ostringstream oss;
    oss << unitTypeName(u.getType());

    if (u.isEmbarked()) {
        const UnitType base = u.getEmbarkedBaseType();
        if (base != UnitType::Unknown) {
            oss << " (" << unitTypeName(base) << ")";
        }
    }

    oss << "\nHP: " << u.getHealth() << "/" << u.getMaxHealth();

    oss << std::fixed << std::setprecision(2);
    oss << "\nATK: " << u.getAttack() << "  DEF: " << u.getDefense();

    oss.unsetf(std::ios::floatfield);
    oss << "\nRNG: " << u.getRange()
        << "  MP: " << u.getMovePoints()
        << "  VIS: " << u.getVisionRange();

    oss << "\nKills: " << u.getKillCounter()
        << "  Vet: " << (u.isVeteran() ? 1 : 0)
        << "  Poison: " << (u.getIsPoisoned() ? 1 : 0);

    return oss.str();
}


static const char* buildingTypeName(BuildingTypeEnum b) {
    switch (b) {
        case BuildingTypeEnum::Farm:        return "Farm";
        case BuildingTypeEnum::Mine:        return "Mine";
        case BuildingTypeEnum::LumberHut:   return "Lumber Hut";
        case BuildingTypeEnum::Port:        return "Port";
        case BuildingTypeEnum::Market:      return "Market";
        case BuildingTypeEnum::Forge:       return "Forge";
        case BuildingTypeEnum::Sawmill:     return "Sawmill";
        case BuildingTypeEnum::Windmill:    return "Windmill";
        case BuildingTypeEnum::Lighthouse:  return "Lighthouse";
        default:                            return "None";
    }
}

static std::vector<std::string> buildingCandidates(BuildingTypeEnum b) {
    const std::string root = "assets/Polytopia_game_engine_textures/common/";
    std::vector<std::string> out;
    out.reserve(8);

    auto add = [&](const std::string& file) {
        out.push_back(root + file + ".png");
        out.push_back(root + file + ".PNG");
    };

    switch (b) {
        case BuildingTypeEnum::Farm:        add("Farm"); break;
        case BuildingTypeEnum::Mine:        add("Mine"); break;
        case BuildingTypeEnum::LumberHut:   add("Lumber_Hut"); break;
        case BuildingTypeEnum::Port:        add("Port"); break;
        case BuildingTypeEnum::Market:      add("Market01"); break;
        case BuildingTypeEnum::Forge:       add("Forge_1"); break;
        case BuildingTypeEnum::Sawmill:     add("Sawmill_1"); break;
        case BuildingTypeEnum::Windmill:    add("Windmill_0"); break;
        case BuildingTypeEnum::Lighthouse:  add("Lighthouse"); break;
        default: break;
    }

    return out;
}


std::vector<std::string> MapRenderer::tribeFolderCandidates(TribeType t) const {
    // Map generator JS expects folders like: "Xin-xi", "Ai-mo" etc.
    // On some local copies people use underscores or no separator, so we try a few candidates.
    switch (t) {
        case TribeType::XinXi:    return {"Xin-xi", "Xin_xi", "XinXi"};
        case TribeType::AiMo:     return {"Ai-mo", "Ai_mo", "AiMo"};
        default: break;
    }

    switch (t) {
        case TribeType::Imperius: return {"Imperius"};
        case TribeType::Bardur:   return {"Bardur"};
        case TribeType::Oumaji:   return {"Oumaji"};
        case TribeType::Kickoo:   return {"Kickoo"};
        case TribeType::Hoodrick: return {"Hoodrick"};
        case TribeType::Luxidoor: return {"Luxidoor"};
        case TribeType::Vengir:   return {"Vengir"};
        case TribeType::Zebasi:   return {"Zebasi"};
        case TribeType::Quetzali: return {"Quetzali"};
        case TribeType::Yadakk:   return {"Yadakk"};
        case TribeType::Aquarion: return {"Aquarion"};
        case TribeType::Elyrion:  return {"Elyrion"};
        case TribeType::Polaris:  return {"Polaris"};
        case TribeType::Cymanti:  return {"Cymanti"};
        default:
            // fallback to something valid so we still render
            return {"Imperius"};
    }
}

std::vector<std::string> MapRenderer::tileCandidates(TribeType tribe, const std::string& kind) const {
    // New pack layout:
    //   assets/Polytopia_game_engine_textures/tribes/<Tribe>/
    //     tile.png (ground), forest.png, mountain.png, fruit.png, head.png, animal.png
    // Older layouts are kept as fallbacks.

    std::vector<std::string> out;

    // Map logical "kind" used by the renderer to actual filenames in the new pack.
    // Renderer uses: ground/forest/mountain/fruit/head (and sometimes others).
    std::vector<std::string> fileNames;
    if (kind == "ground") {
        // Our pack uses tile.png as the base ground tile.
        fileNames = {"tile", "ground"};
    } else {
        fileNames = {kind};
    }

    const auto folders = tribeFolderCandidates(tribe);
    for (const auto& f : folders) {
        // --- New layout (preferred) ---
        for (const auto& fn : fileNames) {
            out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/" + fn + ".png");
            out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/" + fn + ".PNG");
        }

        // --- Older layouts (fallback) ---
        // Primary (JS)
        out.push_back("assets/" + f + "/" + f + " " + kind + ".png");
        out.push_back("assets/" + f + "/" + f + " " + kind + ".PNG");

        // Your old layout
        out.push_back("assets/textures/" + f + "/" + f + " " + kind + ".png");
        out.push_back("assets/textures/" + f + "/" + f + " " + kind + ".PNG");

        // Extra fallbacks (some packs omit the tribe prefix)
        out.push_back("assets/" + f + "/" + kind + ".png");
        out.push_back("assets/" + f + "/" + kind + ".PNG");
        out.push_back("assets/textures/" + f + "/" + kind + ".png");
        out.push_back("assets/textures/" + f + "/" + kind + ".PNG");
    }

    return out;
}

std::vector<std::string> MapRenderer::globalCandidates(const std::string& name) const {
    // New pack layout provides globals under:
    //   assets/Polytopia_game_engine_textures/common/
    //   assets/Polytopia_game_engine_textures/misc/
    //   assets/Polytopia_game_engine_textures/water/
    // We keep older fallbacks too.

    std::vector<std::string> out;

    auto addBothCases = [&](const std::string& p) {
        out.push_back(p + ".png");
        out.push_back(p + ".PNG");
    };

    const std::string root = "assets/Polytopia_game_engine_textures/";

    // New layout (preferred)
    addBothCases(root + "common/" + name);
    addBothCases(root + "misc/" + name);
    addBothCases(root + "water/" + name);

    // Common capitalization variants (e.g. Village.png)
    if (name == "village") {
        addBothCases(root + "common/Village");
        addBothCases(root + "misc/Village");
        // old packs sometimes used capitalized files too
        addBothCases("assets/Village");
        addBothCases("assets/textures/Village");
    }

    // Older layouts (fallback)
    addBothCases("assets/" + name);
    addBothCases("assets/textures/" + name);

    return out;
}

const sf::Texture& MapRenderer::pickFirstExisting(const std::vector<std::string>& paths)
{
    const std::string found = firstExistingWithParents(paths);
    if (!found.empty()) return tex.get(found);

    // Nothing exists on disk: log what we wanted and what we tried (once per logical key),
    // then return an in-memory checkerboard so we don't spam file-load attempts.
    logMissingOnce(paths);
    return fallbackMissingTexture();
}

void MapRenderer::drawSprite(sf::RenderTarget& rt, const sf::Texture& t, float x, float y, float size) {
    sf::Sprite s;
    s.setTexture(t, true);

    const auto ts = t.getSize();
    if (ts.x == 0 || ts.y == 0) return;

    // match JS: drawImage(img, x, y, tile_size, img.height * tile_size / img.width)
    const float scale = size / float(ts.x);
    s.setScale(scale, scale);
    s.setPosition(x, y);

    rt.draw(s);
}

void MapRenderer::draw(sf::RenderTarget& rt) {
    if (!game) return;
    // Ensure UI (right panel / bottom bar) uses window pixel coordinates.
    rt.setView(rt.getDefaultView());

    const Map& map = game->getMap();

    // Map View: if a tile is selected and belongs to a city territory, highlight that city's territory.
    CityId highlightCity = kNoCity;
    if (showOverview && selectedValid && map.inBounds(selectedPos)) {
        const Tile& st = map.at(selectedPos);
        if (st.getTerritoryCityId() != kNoCity) {
            highlightCity = st.getTerritoryCityId();
        } else if (st.getSettlementType() == SettlementTypeEnum::City) {
            highlightCity = static_cast<CityId>(st.getSettlementId());
        }
    }

    // We show the right-side panel in BOTH gameplay and Map View.
    // Therefore the visible map area is the window minus the panel width.
    const sf::Vector2u fullRt = rt.getSize();
    const float panelW = 400.f;

    sf::Vector2u mapRt = fullRt;
    if (mapRt.x > static_cast<unsigned>(panelW + kLeftSpawnPanelW)) {
        mapRt.x = static_cast<unsigned>(float(mapRt.x) - panelW - kLeftSpawnPanelW);
    }
    lastRtSize = mapRt;
    // JS uses a single "tile_size".
    const float tileSize = float(tilePx) * zoom;


    // JS computes Y step using the Xin-xi ground texture dimensions and a hard-coded ratio.
    // We do the same (fallback is still fine if textures are missing).
    const sf::Texture& refGround = pickFirstExisting(tileCandidates(TribeType::XinXi, "ground"));
    const auto refSz = refGround.getSize();
    const float refW = refSz.x ? float(refSz.x) : 1.f;
    const float refH = refSz.y ? float(refSz.y) : 1.f;
    const float scaledTileH = refH * tileSize / refW;
    const float yStep = scaledTileH * (606.f / 1908.f);

    // --- Auto-centering ---
    // Compute the bounding box of the isometric grid in "raw" coordinates (without origin),
    // then shift it so it's centered in the render target. `origin` is treated as a user pan offset.
    float minX = 1e30f, minY = 1e30f;
    float maxX = -1e30f, maxY = -1e30f;

    for (int r = 0; r < map.getHeight(); ++r) {
        for (int c = 0; c < map.getWidth(); ++c) {
            const float xRaw = -tileSize / 2.f + (float(c - r) * tileSize / 2.f);
            const float yRaw = (float(c + r) * yStep);

            // Base tile bounds (good approximation for all tiles)
            minX = std::min(minX, xRaw);
            minY = std::min(minY, yRaw );
            maxX = std::max(maxX, xRaw + tileSize);
            maxY = std::max(maxY, yRaw + scaledTileH);
        }
    }

    const sf::Vector2u rtSz = mapRt;
    const float contentW = std::max(1.f, maxX - minX);
    const float contentH = std::max(1.f, maxY - minY);

    const float centerShiftX = (float(rtSz.x) - contentW) * 0.5f - minX;
    const float centerShiftY = (float(rtSz.y) - contentH) * 0.5f - minY;

    const sf::Vector2f baseShift{centerShiftX + origin.x + kLeftSpawnPanelW, centerShiftY + origin.y};
    // --- Fog-of-war (gameplay view only) ---
    // In Map View (overview) we show the full map without fog.
    const PlayerId curPid = game->getCurrentPlayerId();

    // Preload fog texture once per frame (only used in gameplay view).
    const sf::Texture* fogTex = nullptr;
    if (!showOverview) {
        fogTex = &pickFirstExisting({
            "assets/Polytopia_game_engine_textures/common/fog.png",
            "assets/Polytopia_game_engine_textures/common/fog.PNG"
        });
    }

    // --- Right-side info panel background ---
    const float panelPad = 12.f;
    const sf::Vector2u rtSzPanel = rt.getSize();
    const sf::FloatRect panelRect(float(rtSzPanel.x) - panelW, 0.f, panelW, float(rtSzPanel.y));


    // --- Units lookup (by tile pos) ---
    std::unordered_map<uint32_t, std::vector<const Unit*>> unitsByPos;
    {
        const auto& units = game->getUnits();
        unitsByPos.reserve(units.size() * 2 + 1);
        for (const Unit& u : units) {
            const Pos up = u.getPos();
            if (up.x < 0 || up.y < 0 || up.x >= map.getWidth() || up.y >= map.getHeight()) continue;
            const uint32_t key = (uint32_t(up.y) << 16u) | uint32_t(up.x);
            unitsByPos[key].push_back(&u);
        }
    }

    // --- Road overlay segments (drawn above resources/buildings but below units) ---
    sf::VertexArray roadLines(sf::Triangles);
    roadLines.clear();

    struct UnitDrawCmd {
        const Unit* u = nullptr;
        TribeType tribeSource = TribeType::Unknown;
        float ux = 0.f;
        float uy = 0.f;
        float s  = 0.f;
        int stackIdx = 0;
    };
    std::vector<UnitDrawCmd> unitDraws;
    unitDraws.reserve(unitsByPos.size() * 2 + 8);

    // Unit texture candidates for the new pack layout:
    // assets/Polytopia_game_engine_textures/tribes/<Tribe>/units/<unit>.png
    auto unitTexture = [&](TribeType tribe, UnitType ut) -> const sf::Texture& {
        std::vector<std::string> out;
        const std::string unitFile = unitTypeToFile(ut);
        for (const auto& f : tribeFolderCandidates(tribe)) {
            out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/" + unitFile + ".png");
            out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/" + unitFile + ".PNG");

            // Naval alias fallbacks (some packs use different names)
            if (ut == UnitType::Scout) {
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/scout.png");
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/scout.PNG");
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/ship.png");
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/ship.PNG");
            }
            if (ut == UnitType::Rammer) {
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/ship.png");
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/ship.PNG");
            }
            if (ut == UnitType::Raft) {
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/transportship.png");
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/transportship.PNG");
            }

            // A few common alias fallbacks (optional)
            if (ut == UnitType::MindBender) {
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/mindbender.png");
                out.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/units/mindbender.PNG");
            }
        }
        return pickFirstExisting(out);
    };

    // (Left spawn panel block moved below, after all other rendering)

    // Helpers identical to JS naming.
    auto isLandLike = [&](const Tile& t) {
        const auto bt = t.getBaseTerrain();
        return bt == BaseTerrainEnum::Land || bt == BaseTerrainEnum::Mountain;
    };

    for (int row = 0; row < map.getHeight(); ++row) {
        for (int column = 0; column < map.getWidth(); ++column) {
            const Pos p{column, row};
            const Tile& tile = map.at(p);

            // JS isometric projection (raw), then apply our centering shift.
            const float x = baseShift.x - tileSize / 2.f + (float(column - row) * tileSize / 2.f);
            const float y = baseShift.y + (float(column + row) * yStep);

            // --- Fog-of-war masking (gameplay view only) ---
            // If this tile is not visible to the current player, draw fog and skip its contents.
            if (!showOverview) {
                const VisibilityEnum vis = tile.getVisibility();
                const bool visible = isRevealed(vis, static_cast<PlayerIndex>(curPid));

                if (!visible) {
                    if (fogTex) {
                        // x/y must already be computed for this tile at this point.
                        drawSprite(rt, *fogTex, x, y, tileSize);
                    }
                    continue; // IMPORTANT: don't draw ground/resources/units/settlements under fog
                }
            }

            const TribeType tribe = tile.getTribe();

            // Convert engine tile state to JS-like "type".
            const auto res = tile.getResource();
            const auto resU = static_cast<uint16_t>(res);
            auto hasRes = [&](ResourcesEnum r) {
                return (resU & static_cast<uint16_t>(r)) != 0;
            };

            std::string type;
            switch (tile.getBaseTerrain()) {
                case BaseTerrainEnum::Ocean: type = "ocean"; break;
                case BaseTerrainEnum::Water: type = "water"; break;
                case BaseTerrainEnum::Mountain: type = "mountain"; break;
                case BaseTerrainEnum::Land:
                default:
                    type = hasRes(ResourcesEnum::Forest) ? "forest" : "ground";
                    break;
            }

            // --- draw base (mirrors JS) ---
            if (type == "forest" || type == "mountain") {
                // JS: draw ground first, then overlay forest/mountain with lowering.
                {
                    const sf::Texture& tGround = pickFirstExisting(tileCandidates(tribe, "ground"));
                    drawSprite(rt, tGround, x, y, tileSize);
                }

                const sf::Texture& tOver = pickFirstExisting(tileCandidates(tribe, type));
                const auto oSz = tOver.getSize();
                const float oW = oSz.x ? float(oSz.x) : 1.f;
                const float oH = oSz.y ? float(oSz.y) : 1.f;

                const bool isKickoo = (tribe == TribeType::Kickoo);

                // Overlay vertical placement:
                // - smaller lowering => drawn higher
                // - larger lowering  => drawn lower
                float lowering;
                if (type == "mountain") {
                    // Mountains sit a bit lower for better grounding
                    lowering = isKickoo ? 0.92f : 0.92f;
                } else {
                    // Forests slightly higher than before
                    lowering = 0.6f;
                }

                // JS:
                // y + lowering*tile_size - tile_size * overlay.height / overlay.width
                const float y2 = y + lowering * tileSize - (tileSize * (oH / oW));
                drawSprite(rt, tOver, x, y2, tileSize);
            } else if (type == "water" || type == "ocean") {
                // Water/ocean should be rendered slightly lower than land tiles
                const sf::Texture& t = pickFirstExisting(globalCandidates(type));
                drawSprite(rt, t, x, y , tileSize);
            } else {
                // ground
                const sf::Texture& t = pickFirstExisting(tileCandidates(tribe, type));
                drawSprite(rt, t, x, y, tileSize);
            }

            // Map View: highlight territory of the selected city with a light red filter.
            if (showOverview && highlightCity != kNoCity) {
                if (tile.getTerritoryCityId() == highlightCity) {
                    drawTileDiamondOverlay(rt, x, y, tileSize, yStep, sf::Color(255, 60, 60, 55));
                }
            }

            // --- movement targets overlay (gameplay view only) ---
            if (!showOverview && g_moveSelectedUnit != kNoUnit && g_moveOverlayValid) {
                const uint32_t key = (uint32_t(row) << 16u) | uint32_t(column);
                if (g_moveOverlaySet.find(key) != g_moveOverlaySet.end()) {
                    // Resolves to: assets/Polytopia_game_engine_textures/misc/moveTarget.png
                    const sf::Texture& mt = pickFirstExisting(globalCandidates("moveTarget"));
                    const float s = tileSize * 0.78f;
                    drawSprite(rt, mt,
                               x + (tileSize - s) * 0.5f,
                               y + (tileSize - s) * 0.5f - 0.10f * tileSize,
                               s);
                }
            }

            // --- bridge tile overlay (above water, below resources/settlements/units) ---
            if (tile.getRoadBridge() == RoadBridgeEnum::Bridge) {
                const sf::Texture& br = pickFirstExisting({
                    "assets/Polytopia_game_engine_textures/common/bridge.png",
                    "assets/Polytopia_game_engine_textures/common/bridge.PNG"
                });
                // Lekki lift, żeby ładnie siadło na wodzie
                drawSprite(rt, br, x, y - 0.05f * tileSize, tileSize);
            }

            // --- draw "above" layer (resources/settlements) ---
            // We try to mirror JS priorities:
            // capital head > village > game/fruit (tribe) > crop/fish/metal (global) > ruin > starfish

            // capital: a City can be a capital, but not every City is a capital.
            bool isCapital = false;
            if (tile.getSettlementType() == SettlementTypeEnum::City) {
                if (const City* cObj = resolveCityForTile(game, tile)) {
                    isCapital = cObj->isCapitalCity();
                }
            }
            const bool isVillage = (tile.getSettlementType() == SettlementTypeEnum::Village);
            const bool isRuin = (tile.getSettlementType() == SettlementTypeEnum::Ruin);

            if (tile.getSettlementType() == SettlementTypeEnum::City) {
                int cityLevel = 1;
                TribeType cityTribe = tribe; // default: biome tribe (fallback)

                if (const City* cObj = resolveCityForTile(game, tile)) {
                    cityLevel = std::max(1, int(cObj->getLevel()));
                    // City visuals must match the owning player's tribe.
                    cityTribe = game->getPlayer(static_cast<PlayerId>(cObj->getOwnerId())).getTribeType();
                }

                std::vector<std::string> cityPaths;
                for (const auto& f : tribeFolderCandidates(cityTribe)) {
                    // New layout: tribes/<Tribe>/cities/city_<level>.png
                    cityPaths.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/cities/city_" + std::to_string(cityLevel) + ".png");
                    cityPaths.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/cities/city_" + std::to_string(cityLevel) + ".PNG");

                    // Fallback to level 1 if a specific level sprite is missing
                    cityPaths.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/cities/city_1.png");
                    cityPaths.push_back("assets/Polytopia_game_engine_textures/tribes/" + f + "/cities/city_1.PNG");

                    // Older layout fallback
                    cityPaths.push_back("assets/textures/" + f + "/City/" + f + " city " + std::to_string(cityLevel) + ".png");
                    cityPaths.push_back("assets/textures/" + f + "/City/" + f + " city " + std::to_string(cityLevel) + ".PNG");
                    cityPaths.push_back("assets/textures/" + f + "/City/" + f + " city 1.png");
                    cityPaths.push_back("assets/textures/" + f + "/City/" + f + " city 1.PNG");
                }

                const sf::Texture& city = pickFirstExisting(cityPaths);
                // Lift cities slightly more
                drawSprite(rt, city, x, y - 0.58f * tileSize, tileSize);
            } else if (isVillage) {
                const sf::Texture& t = pickFirstExisting(globalCandidates("village"));
                const float s = tileSize * 0.85f;
                // Slightly smaller + a bit higher
                drawSprite(rt, t,
                           x + (tileSize - s) * 0.5f,
                           y + (tileSize - s) * 0.5f - 0.24f * tileSize,
                           s);
            } else if (hasRes(ResourcesEnum::Fruit)) {
                // Draw fruit slightly smaller and lifted
                const sf::Texture& t = pickFirstExisting(tileCandidates(tribe, "fruit"));
                const float s = tileSize * 0.8f;
                drawSprite(rt, t,
                           x + (tileSize - s) * 0.5f,
                           y + (tileSize - s) * 0.5f - 0.07f * tileSize,
                           s);
            } else if (hasRes(ResourcesEnum::Crops)) {
                const sf::Texture& t = pickFirstExisting(globalCandidates("crop"));
                drawSprite(rt, t, x, y, tileSize);
            } else if (hasRes(ResourcesEnum::Fish)) {
                const sf::Texture& t = pickFirstExisting(globalCandidates("fish"));
                drawSprite(rt, t, x, y, tileSize);
            } else if (hasRes(ResourcesEnum::Metal)) {
                // Draw metal slightly smaller and lifted
                const sf::Texture& t = pickFirstExisting(globalCandidates("metal"));
                const float s = tileSize * 0.5f;
                drawSprite(rt, t,
                           x + (tileSize - s) * 0.5f,
                           y + (tileSize - s) * 0.5f - 0.12f * tileSize,
                           s);
            } else if (isRuin) {
                const sf::Texture& t = pickFirstExisting(globalCandidates("ruin"));
                drawSprite(rt, t, x, y, tileSize);
            }

            // Replace whale with starfish (as you requested).
            if (tile.getSettlementType() == SettlementTypeEnum::Starfish) {                // Draw starfish slightly smaller and lifted
                const sf::Texture& t = pickFirstExisting(globalCandidates("starfish"));
                const float s = tileSize * 0.75f;
                drawSprite(rt, t,
                           x + (tileSize - s) * 0.5f,
                           y + (tileSize - s) * 0.5f - 0.06f * tileSize,
                           s);
            }
            // --- buildings ---
            if (tile.getBuildingType() != BuildingTypeEnum::None) {
                const BuildingTypeEnum btt = tile.getBuildingType();
                const sf::Texture& bt = pickFirstExisting(buildingCandidates(btt));

                // Ports are tall and can disappear if drawn too low; lift them more.
                float lift = 0.0f; // general buildings
                if (btt == BuildingTypeEnum::Port) lift = 0.0f;

                const float s = tileSize * 0.95f;
                drawSprite(rt, bt,
                           x + (tileSize - s) * 0.5f,
                           y + (tileSize - s) * 0.5f - lift * tileSize,
                           s);
            }

            // --- roads overlay (white paths) ---
            // Draw connections between adjacent road tiles (including diagonals).
            if (tile.getRoadBridge() == RoadBridgeEnum::Road || tile.getRoadBridge() == RoadBridgeEnum::Bridge) {
                const sf::Vector2f c0{x + tileSize * 0.5f, y + yStep}; // center of diamond
                const bool curIsBridge = (tile.getRoadBridge() == RoadBridgeEnum::Bridge);

                auto addSeg = [&](int dx, int dy) {
                    const Pos p2{column + dx, row + dy};
                    if (!map.inBounds(p2)) return;
                    const Tile& t2 = map.at(p2);
                    const RoadBridgeEnum rb2 = t2.getRoadBridge();
                    if (!(rb2 == RoadBridgeEnum::Road || rb2 == RoadBridgeEnum::Bridge)) return;

                    // Nigdy nie rysuj przekątnych, jeśli po drodze jest bridge (z którejkolwiek strony)
                    if ((dx != 0 && dy != 0) && (curIsBridge || rb2 == RoadBridgeEnum::Bridge)) return;
                    // To avoid duplicates, only connect to "forward" neighbors.
                    if (dy < 0) return;
                    if (dy == 0 && dx <= 0) return;

                    const float x2 = baseShift.x - tileSize / 2.f + (float(p2.x - p2.y) * tileSize / 2.f);
                    const float y2 = baseShift.y + (float(p2.x + p2.y) * yStep);
                    const sf::Vector2f c1{x2 + tileSize * 0.5f, y2 + yStep};

                    const sf::Vector2f d = c1 - c0;
                    const float len2 = d.x*d.x + d.y*d.y;
                    if (len2 < 1e-6f) return;

                    const float invLen = 1.f / std::sqrt(len2);
                    const sf::Vector2f n{-d.y * invLen, d.x * invLen}; // prostopadły

                    const float w = std::max(1.6f, tileSize * 0.055f);
                    const sf::Vector2f off = n * (w * 0.5f);

                    const sf::Color col(250, 250, 250, 230);

                    const sf::Vector2f a = c0 + off;
                    const sf::Vector2f b = c1 + off;
                    const sf::Vector2f c = c1 - off;
                    const sf::Vector2f d2 = c0 - off;

                    // (a,b,c) + (a,c,d2)
                    roadLines.append(sf::Vertex(a, col));
                    roadLines.append(sf::Vertex(b, col));
                    roadLines.append(sf::Vertex(c, col));
                    roadLines.append(sf::Vertex(a, col));
                    roadLines.append(sf::Vertex(c, col));
                    roadLines.append(sf::Vertex(d2, col));                };

                // 4 directions cover all undirected 8-neighborhood pairs (incl. diagonals)
                // Forward neighbors to avoid duplicates.
                addSeg(1, 0);
                addSeg(0, 1);

                // Roads mogą łączyć po skosie (Twoja wcześniejsza zasada), ale bridges nigdy.
                if (!curIsBridge) {
                    addSeg(1, 1);
                    addSeg(-1, 1);
                }            }

            // --- attack targets overlay (gameplay view only) ---
            if (!showOverview && g_moveSelectedUnit != kNoUnit && g_attackOverlayValid) {
                const uint32_t key = (uint32_t(row) << 16u) | uint32_t(column);
                if (g_attackOverlaySet.find(key) != g_attackOverlaySet.end()) {
                    // Resolves to: assets/Polytopia_game_engine_textures/misc/attackTarget.png
                    const sf::Texture& at = pickFirstExisting({
                        "assets/Polytopia_game_engine_textures/misc/attackTarget.png",
                        "assets/Polytopia_game_engine_textures/misc/attackTarget.PNG"
                    });
                    const float s = tileSize * 0.78f;
                    drawSprite(rt, at,
                               x + (tileSize - s) * 0.5f,
                               y + (tileSize - s) * 0.5f - 0.10f * tileSize,
                               s);
                }
            }

            // --- capture hint: show above enemy cities that are capturable by a unit standing on them ---
            if (game && tile.getSettlementType() == SettlementTypeEnum::City) {
                const PlayerId curPid = game->getCurrentPlayerId();
                const Player& curPl = game->getPlayer(curPid);

                const City* cObj = resolveCityForTile(game, tile);
                const bool enemyCity = cObj
                    ? (static_cast<PlayerId>(cObj->getOwnerId()) != curPid)
                    : (tile.getTribe() != curPl.getTribeType());

                const UnitId onTile = map.unitOn(p);
                bool canCapture = false;
                if (enemyCity && onTile != Map::kNoUnit) {
                    const Unit* uu = game->getUnit(onTile);
                    if (uu && uu->getOwnerId() == curPid && !uu->movedThisTurn() && !uu->attackedThisTurn()) {
                        canCapture = true;
                    }
                }

                if (canCapture) {
                    // Resolves to assets/Polytopia_game_engine_textures/misc/hint.png
                    const sf::Texture& hint = pickFirstExisting(globalCandidates("hint"));
                    const float s = tileSize * 0.70f;
                    drawSprite(rt, hint,
                               x + (tileSize - s) * 0.5f,
                               y - 0.85f * tileSize,
                               s);
                }
            }

            // --- capture hint: show above villages that are capturable by a unit standing on them ---
            if (game && tile.getSettlementType() == SettlementTypeEnum::Village) {
                const PlayerId curPid = game->getCurrentPlayerId();
                const UnitId onTile = map.unitOn(p);

                bool canCaptureVillage = false;
                if (onTile != Map::kNoUnit) {
                    const Unit* uu = game->getUnit(onTile);
                    if (uu && uu->getOwnerId() == curPid && !uu->movedThisTurn() && !uu->attackedThisTurn()) {
                        canCaptureVillage = true;
                    }
                }

                if (canCaptureVillage) {
                    const sf::Texture& hint = pickFirstExisting(globalCandidates("hint"));
                    const float s = tileSize * 0.70f;
                    drawSprite(rt, hint,
                               x + (tileSize - s) * 0.5f,
                               y - 0.85f * tileSize,
                               s);
                }
            }

            // --- queue units on this tile (drawn later, above road lines) ---
            {
                const uint32_t key = (uint32_t(row) << 16u) | uint32_t(column);
                auto itU = unitsByPos.find(key);
                if (itU != unitsByPos.end() && !itU->second.empty()) {
                    int idx = 0;
                    for (const Unit* u : itU->second) {
                        // Unit texture should depend on the owning player's tribe, not the tile's tribe.
                        TribeType tribeSource = tribe;
                        if (game) {
                            const PlayerId owner = u->getOwnerId();
                            tribeSource = game->getPlayer(owner).getTribeType();
                        }

                        // Units are drawn above the tile; scale is based on tileSize (width).
                        const float s = tileSize * 1.5f;
                        const float ux = x + (tileSize - s) * 0.5f;
                        const float uy = y - 0.4f * tileSize - float(idx) * 10.f;

                        unitDraws.push_back(UnitDrawCmd{u, tribeSource, ux, uy, s, idx});

                        ++idx;
                        if (idx >= 2) break; // avoid clutter
                    }
                }
            }
        }
    }

    // --- Draw roads (above resources/buildings, below units) ---
    if (roadLines.getVertexCount() > 0) {
        rt.draw(roadLines);
    }

    // --- Draw queued units (above roads) ---
    for (const UnitDrawCmd& cmd : unitDraws) {
        if (!cmd.u) continue;

        const sf::Texture& utex = unitTexture(cmd.tribeSource, cmd.u->getType());
        drawSprite(rt, utex, cmd.ux, cmd.uy, cmd.s);
    }

    // --- Bottom context actions (gameplay view only) ---
    g_actionBtnsValid = false;
    g_actionBtnCount = 0;

    // GUI does NOT validate gameplay rules. It only offers actions.
    // All validation must happen in Game.cpp / systems.
    if (game) {
        if (!showOverview) {
            const Map& m0 = game->getMap();
            const Pos p0 = selectedValid ? selectedPos : Pos{0, 0};
            if (selectedValid && m0.inBounds(p0)) {
                const Tile& t0 = m0.at(p0);
                // Build a small action bar under the map (left side).
                const float mapW = float(mapRt.x);
                const float mapH = float(rt.getSize().y);
                const float barH = 180.f; // more room: multiple rows of buttons
                const float pad = 10.f;
                auto pushBtn = [&](ActionKind kind, const char* label, UnitType ut, BuildingTypeEnum bt) {
                    if (g_actionBtnCount >= int(g_actionBtns.size())) return;

                    const float bw = 110.f; // narrower buttons so more fit
                    const float bh = 30.f;
                    const float gap = 6.f;
                    const int maxPerRow = std::max(1, int((mapW - 2.f * pad) / (bw + gap)));
                    const int row = g_actionBtnCount / maxPerRow;
                    const int col = g_actionBtnCount % maxPerRow;

                    const float bx = kLeftSpawnPanelW + pad + float(col) * (bw + gap);
                    const float by = (mapH - barH) + pad + float(row) * (bh + 6.f);

                    g_actionBtns[g_actionBtnCount] = ActionButton{sf::FloatRect(bx, by, bw, bh), kind, ut, bt};
                    ++g_actionBtnCount;

                    sf::RectangleShape b;
                    b.setPosition({bx, by});
                    b.setSize({bw, bh});
                    b.setFillColor(sf::Color(35, 35, 35, 235));
                    b.setOutlineThickness(2.f);
                    b.setOutlineColor(sf::Color(110, 110, 110, 255));
                    rt.draw(b);

                    if (ensureUIFontLoaded()) {
                        sf::Text t;
                        t.setFont(uiFont);
                        t.setCharacterSize(16);
                        t.setFillColor(sf::Color(240, 240, 240, 255));
                        t.setString(label);
                        const sf::FloatRect lb = t.getLocalBounds();
                        t.setPosition(
                            bx + (bw - lb.width) * 0.5f - lb.left,
                            by + (bh - lb.height) * 0.5f - lb.top - 1.f
                        );
                        rt.draw(t);
                    }
                };

                // Always show ALL buttons so we can test that Game.cpp validates rules.
                // (GUI is only a launcher.)

                // Capture
                pushBtn(ActionKind::CaptureVillage, "Capture Village", UnitType::Warrior, BuildingTypeEnum::None);
                pushBtn(ActionKind::CaptureCity,    "Capture City",    UnitType::Warrior, BuildingTypeEnum::None);

                // Training

                // Upgrades (Raft -> ship variants)
                pushBtn(ActionKind::UpgradeRaftToScout,  "Upgrade -> Scout",  UnitType::Raft, BuildingTypeEnum::None);
                pushBtn(ActionKind::UpgradeRaftToRammer, "Upgrade -> Rammer", UnitType::Raft, BuildingTypeEnum::None);
                pushBtn(ActionKind::UpgradeRaftToBomber, "Upgrade -> Bomber", UnitType::Raft, BuildingTypeEnum::None);

                // Veteran
                pushBtn(ActionKind::BecomeVeteran, "Become Veteran", UnitType::Unknown, BuildingTypeEnum::None);

                // Buildings
                pushBtn(ActionKind::BuildBuilding, "Build Farm",       UnitType::Unknown, BuildingTypeEnum::Farm);
                pushBtn(ActionKind::BuildBuilding, "Build Mine",       UnitType::Unknown, BuildingTypeEnum::Mine);
                pushBtn(ActionKind::BuildBuilding, "Build Lumber Hut", UnitType::Unknown, BuildingTypeEnum::LumberHut);
                pushBtn(ActionKind::BuildBuilding, "Build Port",       UnitType::Unknown, BuildingTypeEnum::Port);
                pushBtn(ActionKind::BuildBuilding, "Build Market",     UnitType::Unknown, BuildingTypeEnum::Market);
                pushBtn(ActionKind::BuildBuilding, "Build Forge",      UnitType::Unknown, BuildingTypeEnum::Forge);
                pushBtn(ActionKind::BuildBuilding, "Build Sawmill",    UnitType::Unknown, BuildingTypeEnum::Sawmill);
                pushBtn(ActionKind::BuildBuilding, "Build Windmill",   UnitType::Unknown, BuildingTypeEnum::Windmill);
                pushBtn(ActionKind::BuildBuilding, "Build Lighthouse", UnitType::Unknown, BuildingTypeEnum::Lighthouse);

                // Tile actions
                pushBtn(ActionKind::Organization, "Organization", UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::Hunt,         "Hunt",         UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::Fishing,      "Fishing",      UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::ClearForest,  "Clear Forest", UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::BurnForest,   "Burn Forest",  UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::GrowForest,   "Grow Forest",  UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::DestroyTile,  "Destroy",      UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::BuildRoad,    "Build Road",   UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::BuildBridge,  "Build Bridge", UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::Explorer,     "Explorer",     UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::ExploreRuin,     "Explore Ruin",     UnitType::Unknown, BuildingTypeEnum::None);
                pushBtn(ActionKind::CollectStarfish, "Collect Starfish", UnitType::Unknown, BuildingTypeEnum::None);

                if (g_actionBtnCount > 0) {
                    sf::RectangleShape bar;
                    bar.setPosition(kLeftSpawnPanelW, mapH - barH);                    bar.setSize({mapW, barH});
                    bar.setFillColor(sf::Color(15, 15, 15, 210));
                    bar.setOutlineThickness(1.f);
                    bar.setOutlineColor(sf::Color(70, 70, 70, 220));
                    rt.draw(bar);

                    // Re-draw buttons on top of bar
                    const int want = g_actionBtnCount;
                    std::vector<ActionButton> saved;
                    saved.reserve(size_t(want));
                    for (int i = 0; i < want; ++i) saved.push_back(g_actionBtns[i]);

                    g_actionBtnCount = 0;
                    for (const ActionButton& sbtn : saved) {
                        if (sbtn.kind == ActionKind::CaptureVillage) {
                            pushBtn(sbtn.kind, "Capture Village", sbtn.trainType, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::CaptureCity) {
                            pushBtn(sbtn.kind, "Capture City", sbtn.trainType, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::TrainUnit) {
                            pushBtn(sbtn.kind,
                                    (sbtn.trainType == UnitType::Rider) ? "Train Rider" : "Train Warrior",
                                    sbtn.trainType,
                                    BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::BuildBuilding) {
                            std::string lbl = std::string("Build ") + buildingTypeName(sbtn.buildType);
                            pushBtn(sbtn.kind, lbl.c_str(), UnitType::Unknown, sbtn.buildType);
                        } else if (sbtn.kind == ActionKind::ClearForest) {
                            pushBtn(sbtn.kind, "Clear Forest", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::BurnForest) {
                            pushBtn(sbtn.kind, "Burn Forest", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::GrowForest) {
                            pushBtn(sbtn.kind, "Grow Forest", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::Hunt) {
                            pushBtn(sbtn.kind, "Hunt", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::Fishing) {
                            pushBtn(sbtn.kind, "Fishing", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::DestroyTile) {
                            pushBtn(sbtn.kind, "Destroy", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::BuildRoad) {
                            pushBtn(sbtn.kind, "Build Road", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::Organization) {
                            pushBtn(sbtn.kind, "Organization", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::UpgradeRaftToScout) {
                            pushBtn(sbtn.kind, "Upgrade -> Scout", UnitType::Raft, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::UpgradeRaftToRammer) {
                            pushBtn(sbtn.kind, "Upgrade -> Rammer", UnitType::Raft, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::UpgradeRaftToBomber) {
                            pushBtn(sbtn.kind, "Upgrade -> Bomber", UnitType::Raft, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::BecomeVeteran) {
                            pushBtn(sbtn.kind, "Become Veteran", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::BuildBridge) {
                            pushBtn(sbtn.kind, "Build Bridge", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::Explorer) {
                            pushBtn(sbtn.kind, "Explorer", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::ExploreRuin) {
                            pushBtn(sbtn.kind, "Explore Ruin", UnitType::Unknown, BuildingTypeEnum::None);
                        } else if (sbtn.kind == ActionKind::CollectStarfish) {
                            pushBtn(sbtn.kind, "Collect Starfish", UnitType::Unknown, BuildingTypeEnum::None);
                        }

                        g_actionPos = p0;
                        g_actionBtnsValid = true;

                    }
                }
            }
        }
        // --- Draw selection highlight (on top) ---
        // --- Draw selection highlight (on top) ---
        if (selectedValid) {
            const int row = selectedPos.y;
            const int column = selectedPos.x;

            const float x = baseShift.x - tileSize / 2.f + (float(column - row) * tileSize / 2.f);
            const float y = baseShift.y + (float(column + row) * yStep);

            // Match the visible grid cell (diamond height is 2*yStep).
            const sf::Vector2f top  {x + tileSize * 0.5f, y};
            const sf::Vector2f right{x + tileSize,        y + yStep};
            const sf::Vector2f bot  {x + tileSize * 0.5f, y + 2.f * yStep};
            const sf::Vector2f left {x,                   y + yStep};

            // Fill (translucent)
            sf::VertexArray fill(sf::TriangleFan, 4);
            fill[0].position = top;
            fill[1].position = right;
            fill[2].position = bot;
            fill[3].position = left;
            for (std::size_t i = 0; i < 4; ++i) fill[i].color = sf::Color(255, 220, 80, 70);
            rt.draw(fill);

            // Outline
            sf::VertexArray outline(sf::LineStrip, 5);
            outline[0].position = top;
            outline[1].position = right;
            outline[2].position = bot;
            outline[3].position = left;
            outline[4].position = top;
            for (std::size_t i = 0; i < 5; ++i) outline[i].color = sf::Color(255, 220, 80, 245);
            rt.draw(outline);

            // “Thicker” feel (second outline slightly offset)
            sf::VertexArray outline2(sf::LineStrip, 5);
            outline2[0].position = top + sf::Vector2f(1.f, 1.f);
            outline2[1].position = right + sf::Vector2f(1.f, 1.f);
            outline2[2].position = bot + sf::Vector2f(1.f, 1.f);
            outline2[3].position = left + sf::Vector2f(1.f, 1.f);
            outline2[4].position = top + sf::Vector2f(1.f, 1.f);
            for (std::size_t i = 0; i < 5; ++i) outline2[i].color = sf::Color(255, 220, 80, 140);
            rt.draw(outline2);
        }

        // --- Right-side info panel background (drawn last, on top) ---
        {
            sf::RectangleShape panelBg;
            panelBg.setPosition(panelRect.left, panelRect.top);
            panelBg.setSize({panelRect.width, panelRect.height});
            panelBg.setFillColor(sf::Color(20, 20, 20, 190));
            panelBg.setOutlineThickness(2.f);
            panelBg.setOutlineColor(sf::Color(70, 70, 70, 220));
            rt.draw(panelBg);
        }

        // --- Map View right panel (tile info + Back) ---
        if (showOverview) {
            const bool hasFont = ensureUIFontLoaded();

            const float bw = panelRect.width - 2.f * panelPad;
            const float bh = 40.f;
            const float baseY = panelRect.top + panelRect.height - panelPad - bh;

            btnBack = sf::FloatRect(panelRect.left + panelPad, baseY, bw, bh);
            btnEndTurn = sf::FloatRect();
            btnOverview = sf::FloatRect();

            // Back button
            {
                sf::RectangleShape b;
                b.setPosition({btnBack.left, btnBack.top});
                b.setSize({btnBack.width, btnBack.height});
                b.setFillColor(sf::Color(35, 35, 35, 220));
                b.setOutlineThickness(2.f);
                b.setOutlineColor(sf::Color(90, 90, 90, 255));
                rt.draw(b);

                if (hasFont) {
                    sf::Text t;
                    t.setFont(uiFont);
                    t.setString("Back");
                    t.setCharacterSize(18);
                    t.setFillColor(sf::Color(240, 240, 240, 255));
                    const sf::FloatRect lb = t.getLocalBounds();
                    t.setPosition(
                        btnBack.left + (btnBack.width - lb.width) * 0.5f - lb.left,
                        btnBack.top  + (btnBack.height - lb.height) * 0.5f - lb.top - 1.f
                    );
                    rt.draw(t);
                }
            }

            auto addLine = [&](float& ty, const std::string& s, unsigned sz = 16) {
                if (!hasFont) return;

                // Support multi-line strings (e.g. unitDisplayString())
                std::string_view v(s);
                while (!v.empty()) {
                    // Split on either '\n' or '\r' and handle CRLF.
                    const size_t brk = v.find_first_of("\r\n");
                    std::string_view line = (brk == std::string_view::npos) ? v : v.substr(0, brk);
                    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

                    sf::Text t;
                    t.setFont(uiFont);
                    t.setString(std::string(line));
                    t.setCharacterSize(sz);
                    t.setFillColor(sf::Color(240, 240, 240, 255));
                    t.setPosition(panelRect.left + panelPad, ty);
                    rt.draw(t);

                    ty += float(sz) + 6.f;

                    if (brk == std::string_view::npos) break;

                    // Consume the line break. If it's CRLF, consume both.
                    const char c = v[brk];
                    size_t adv = 1;
                    if (c == '\r' && brk + 1 < v.size() && v[brk + 1] == '\n') adv = 2;
                    v.remove_prefix(brk + adv);
                }
            };

            float ty = panelPad + 20.f;
            addLine(ty, "===== TILE =====", 18);

            if (!selectedValid) {
                addLine(ty, "(click a tile)", 14);
            } else if (!map.inBounds(selectedPos)) {
                addLine(ty, "(out of bounds)", 14);
            } else {
                const Tile& st = map.at(selectedPos);

                addLine(ty, "Pos: (" + std::to_string(selectedPos.x) + ", " + std::to_string(selectedPos.y) + ")", 14);
                addLine(ty, std::string("Tribe: ") + tribeDisplayName(st.getTribe()), 14);
                addLine(ty, std::string("Terrain: ") + baseTerrainName(st.getBaseTerrain()), 14);
                addLine(ty, std::string("Road: ") + (st.getRoadBridge() == RoadBridgeEnum::Road ? "yes" : "no"), 14);
                addLine(ty, std::string("Settlement: ") + settlementName(st.getSettlementType()), 14);

                // Resources list (Tile)
                {
                    const auto resU = static_cast<uint16_t>(st.getResource());
                    auto hasR = [&](ResourcesEnum r) { return (resU & static_cast<uint16_t>(r)) != 0; };

                    std::string rline = "Resources: ";
                    bool any = false;
                    if (hasR(ResourcesEnum::Forest)) { rline += "Forest "; any = true; }
                    if (hasR(ResourcesEnum::Fruit))  { rline += "Fruit ";  any = true; }
                    if (hasR(ResourcesEnum::Crops))  { rline += "Crops ";  any = true; }
                    if (hasR(ResourcesEnum::Fish))   { rline += "Fish ";   any = true; }
                    if (hasR(ResourcesEnum::Metal))  { rline += "Metal ";  any = true; }
                    if (!any) rline += "None";
                    addLine(ty, rline, 14);
                }

                if (st.getSettlementType() == SettlementTypeEnum::City) {
                    addLine(ty, "===== CITY =====", 18);
                    const City* cObj = resolveCityForTile(game, st);
                    if (cObj) {
                        addLine(ty, "City name: " + cObj->getName(), 14);
                        addLine(ty, "CityId: " + std::to_string(int(cObj->getCityId())), 14);
                        addLine(ty, "OwnerId: " + std::to_string(int(cObj->getOwnerId())), 14);
                        addLine(ty, std::string("Is capital: ") + (cObj->isCapitalCity() ? "yes" : "no"), 14);
                        addLine(ty, "Level: " + std::to_string(int(cObj->getLevel())), 14);
                        addLine(ty, "Population: " + std::to_string(int(cObj->getPopulation())) + "/" + std::to_string(int(cObj->populationNeededToLevelUp())), 14);
                        addLine(ty, "Stars/round: " + std::to_string(int(cObj->getStarsPerRound())), 14);
                        addLine(ty, "Units in city: " + std::to_string(int(cObj->getUnitsCount())) + "/" + std::to_string(int(cObj->maxUnitCapacity())), 14);
                        addLine(ty, std::string("Has workshop: ") + (cObj->hasWorkshopEnabled() ? "yes" : "no"), 14);
                        addLine(ty, std::string("Has city wall: ") + (cObj->hasCityWallEnabled() ? "yes" : "no"), 14);
                    } else {
                        addLine(ty, "(City object not resolved)", 14);
                    }
                }

                // Unit info (if any)
                {
                    addLine(ty, "===== UNIT =====", 18);
                    const uint32_t skey = (uint32_t(selectedPos.y) << 16u) | uint32_t(selectedPos.x);
                    auto itU = unitsByPos.find(skey);
                    if (itU != unitsByPos.end() && !itU->second.empty()) {
                        const Unit* u = itU->second.front();
                        addLine(ty, std::string("Unit: ") + unitDisplayString(*u), 14);
                        addLine(ty, "Owner: " + std::to_string(int(u->getOwnerId())), 14);
                        addLine(ty, "HP: " + std::to_string(u->getHealth()) + "/" + std::to_string(u->getMaxHealth()), 14);
                        if (itU->second.size() > 1) {
                            addLine(ty, "(+" + std::to_string(itU->second.size() - 1) + " more)", 14);
                        }
                    } else {
                        addLine(ty, "Unit: none", 14);
                    }
                }
            }

            return; // Map View panel drawn; skip gameplay panel below
        }

        if (!showOverview) {
            // Default: no tech hitboxes unless we successfully render the tech tree below.
            g_techHitValid = false;
            g_techHitPos.clear();
            // ================= FONTLESS UI =================
            const bool hasFont = ensureUIFontLoaded();
            // Buttons area (bottom of right panel)
            const float bw = panelRect.width - 2.f * panelPad;
            const float bh = 40.f;
            const float baseY = panelRect.top + panelRect.height - panelPad - (bh * 2.f + 10.f);

            auto addLine = [&](float& ty, const std::string& s, unsigned sz = 16) {
                if (!hasFont) return;

                // Support multi-line strings (e.g. unitDisplayString())
                std::string_view v(s);
                while (!v.empty()) {
                    // Split on either '\n' or '\r' and handle CRLF.
                    const size_t brk = v.find_first_of("\r\n");
                    std::string_view line = (brk == std::string_view::npos) ? v : v.substr(0, brk);
                    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

                    sf::Text t;
                    t.setFont(uiFont);
                    t.setString(std::string(line));
                    t.setCharacterSize(sz);
                    t.setFillColor(sf::Color(240, 240, 240, 255));
                    t.setPosition(panelRect.left + panelPad, ty);
                    rt.draw(t);

                    ty += float(sz) + 6.f;

                    if (brk == std::string_view::npos) break;

                    // Consume the line break. If it's CRLF, consume both.
                    const char c = v[brk];
                    size_t adv = 1;
                    if (c == '\r' && brk + 1 < v.size() && v[brk + 1] == '\n') adv = 2;
                    v.remove_prefix(brk + adv);
                }
            };

            float yAfterPlayers = panelPad + 30.f;

            btnEndTurn  = sf::FloatRect(panelRect.left + panelPad, baseY, bw, bh);
            btnOverview = sf::FloatRect(panelRect.left + panelPad, baseY + bh + 10.f, bw, bh);
            btnBack     = btnOverview;

            auto drawBtn = [&](const sf::FloatRect& r, bool active, const char* label) {
                sf::RectangleShape b;
                b.setPosition({r.left, r.top});
                b.setSize({r.width, r.height});
                b.setFillColor(active ? sf::Color(55, 55, 55, 235) : sf::Color(35, 35, 35, 220));
                b.setOutlineThickness(2.f);
                b.setOutlineColor(sf::Color(90, 90, 90, 255));
                rt.draw(b);

                if (hasFont && label) {
                    sf::Text t;
                    t.setFont(uiFont);
                    t.setString(label);
                    t.setCharacterSize(18);
                    t.setFillColor(sf::Color(240, 240, 240, 255));

                    const sf::FloatRect lb = t.getLocalBounds();
                    t.setPosition(
                        r.left + (r.width - lb.width) * 0.5f - lb.left,
                        r.top  + (r.height - lb.height) * 0.5f - lb.top - 1.f
                    );
                    rt.draw(t);
                }
            };

            drawBtn(btnEndTurn, false, "End Turn");
            drawBtn(btnOverview, false, "Map View");

            // Top indicators (turn + current player)
            {
                sf::RectangleShape bar;
                bar.setPosition(panelRect.left + panelPad, panelPad);
                bar.setSize({panelRect.width - 2.f * panelPad, 10.f});
                bar.setFillColor(sf::Color(255, 220, 80, 180));
                rt.draw(bar);

                const int pid = int(game->getCurrentPlayerId());
                sf::RectangleShape cur;
                cur.setPosition(panelRect.left + panelPad, panelPad + 14.f);
                cur.setSize({panelRect.width - 2.f * panelPad, 6.f});
                cur.setFillColor(sf::Color(80 + (pid * 60) % 160, 80 + (pid * 120) % 160, 80 + (pid * 200) % 160, 220));
                rt.draw(cur);
            }
            {
                float ty = panelPad + 22.f;
                addLine(ty,
                        "Turn: " + std::to_string(game->getTurnNumber()) +
                        "   Player: " + std::to_string(int(game->getCurrentPlayerId())) +
                        "   (TAB: Map View)",
                        16);
            }

            // Players list (head icon + stars + income)
            // NOTE: reserve space for the tech tree so everything fits on screen.
            float yAfterPlayersList = panelPad + 56.f;
            {
                const auto& pls = game->getPlayers();
                float y0 = panelPad + 56.f; // leave room for header line
                const float rowH = 30.f;
                const float rowW = panelRect.width - 2.f * panelPad;

                // Reserve a minimum height for the tech tree area.
                // If the window/panel is small or many players are present, we stop listing players earlier.
                const float techMinH = 260.f;
                const float maxYForPlayers = std::max(y0, (baseY - 10.f) - techMinH);

                int shown = 0;
                for (const Player& pl : pls) {
                    if (y0 + rowH > maxYForPlayers) {
                        // Indicate there are more players not shown in the list.
                        sf::RectangleShape bg;
                        bg.setPosition(panelRect.left + panelPad, y0);
                        bg.setSize({rowW, rowH});
                        bg.setFillColor(sf::Color(25, 25, 25, 200));
                        bg.setOutlineThickness(1.f);
                        bg.setOutlineColor(sf::Color(70, 70, 70, 220));
                        rt.draw(bg);

                        if (ensureUIFontLoaded()) {
                            sf::Text t;
                            t.setFont(uiFont);
                            t.setCharacterSize(14);
                            t.setFillColor(sf::Color(200, 200, 200, 255));
                            t.setString("...");
                            t.setPosition(panelRect.left + panelPad + 12.f, y0 + 6.f);
                            rt.draw(t);
                        }

                        y0 += rowH + 6.f;
                        break;
                    }

                    const bool isCur = (pl.getId() == game->getCurrentPlayerId());
                    const int stars  = pl.getStars();
                    const int income = TurnSystem::calcIncomeForPlayer(*game, pl.getId());

                    // Row background
                    sf::RectangleShape bg;
                    bg.setPosition(panelRect.left + panelPad, y0);
                    bg.setSize({rowW, rowH});
                    bg.setFillColor(isCur ? sf::Color(35, 35, 35, 220) : sf::Color(25, 25, 25, 200));
                    bg.setOutlineThickness(1.f);
                    bg.setOutlineColor(sf::Color(70, 70, 70, 220));
                    rt.draw(bg);

                    // Head icon (tribe head.png)
                    const TribeType t = pl.getTribeType();
                    const sf::Texture& headTex = pickFirstExisting(tileCandidates(t, "head"));

                    const float icon = 24.f;
                    const float ix = panelRect.left + panelPad + 8.f;
                    const float iy = y0 + (rowH - icon) * 0.5f;
                    drawSprite(rt, headTex, ix, iy, icon);

                    // Text
                    if (ensureUIFontLoaded()) {
                        sf::Text line;
                        line.setFont(uiFont);
                        line.setCharacterSize(14);
                        line.setFillColor(sf::Color(240, 240, 240, 255));
                        line.setString(
                            "P" + std::to_string(int(pl.getId())) +
                            "  stars=" + std::to_string(stars) +
                            "  income="+std::to_string(income)
                        );
                        line.setPosition(panelRect.left + panelPad + 8.f + icon + 10.f, y0 + 6.f);
                        rt.draw(line);
                    }

                    y0 += rowH + 6.f;
                    ++shown;
                    if (shown >= 10) break; // hard cap, prevents extreme lists
                }

                yAfterPlayersList = y0 + 6.f;
            }

            // Tech tree (visible only in gameplay view, hidden in Map View)
            // IMPORTANT: the render depends on how Tech classes are connected (Tech::previousTech).
            // Owned techs: green. Missing techs: red with current price under the name.
            if (hasFont) {
                const PlayerId curPid = game->getCurrentPlayerId();
                const Player& curPl = game->getPlayer(curPid);

                // Available space between player rows and buttons
                const float left = panelRect.left + panelPad;
                const float right = panelRect.left + panelRect.width - panelPad;
                const float top = yAfterPlayersList + 10.f;
                const float bottom = baseY - 10.f;
                const float w = std::max(10.f, right - left);
                const float h = std::max(10.f, bottom - top);

                // If we don't have enough height, show a small hint instead of a broken tree.
                if (h < 180.f) {
                    float ty = top;
                    addLine(ty, "Techs", 18);
                    addLine(ty, "(resize window to see tree)", 14);
                } else {
                    // Pull all techs from TechDB (new system)
                    std::vector<TechId> techs;
                    techs.reserve(static_cast<size_t>(TechId::Count));
                    for (uint8_t i = 0; i < static_cast<uint8_t>(TechId::Count); ++i) {
                        techs.push_back(static_cast<TechId>(i));
                    }

                    auto techName = [&](TechId t) -> std::string_view {
                        return TechDB::getTech(t).name;
                    };
                    auto byName = [&](TechId a, TechId b) {
                        return techName(a) < techName(b);
                    };

                    // Build children graph from prerequisites
                    std::unordered_map<TechId, std::vector<TechId>> children;
                    children.reserve(techs.size() * 2 + 1);

                    std::vector<TechId> roots;
                    roots.reserve(8);

                    for (TechId t : techs) {
                        TechId p = TechDB::getPrerequisite(t);
                        if (p == TechId::Count) roots.push_back(t);
                        else children[p].push_back(t);
                    }

                    // Preferred child ordering (Polytopia layout)
                    std::unordered_map<TechId, std::unordered_map<TechId, int>> prefChild;
                    prefChild.reserve(32);

                    auto setChildOrder = [&](TechId parent, std::initializer_list<TechId> orderedChildren) {
                        auto& m = prefChild[parent];
                        int idx = 0;
                        for (TechId c : orderedChildren) m[c] = idx++;
                    };

                    setChildOrder(TechId::Hunting,      {TechId::Forestry, TechId::Archery});
                    setChildOrder(TechId::Forestry,     {TechId::Mathematics});
                    setChildOrder(TechId::Archery,      {TechId::Spiritualism});

                    setChildOrder(TechId::Riding,       {TechId::Roads, TechId::FreeSpirit});
                    setChildOrder(TechId::Roads,        {TechId::Trade});
                    setChildOrder(TechId::FreeSpirit,   {TechId::Chivalry});

                    setChildOrder(TechId::Organization, {TechId::Farming, TechId::Strategy});
                    setChildOrder(TechId::Farming,      {TechId::Construction});
                    setChildOrder(TechId::Strategy,     {TechId::Diplomacy});

                    setChildOrder(TechId::Fishing,      {TechId::Ramming, TechId::Sailing});
                    setChildOrder(TechId::Sailing,      {TechId::Navigation});
                    setChildOrder(TechId::Ramming,      {TechId::Aquatism});

                    setChildOrder(TechId::Climbing,     {TechId::Mining, TechId::Meditation});
                    setChildOrder(TechId::Mining,       {TechId::Smithery});
                    setChildOrder(TechId::Meditation,   {TechId::Philosophy});

                    auto childLess = [&](TechId parent, TechId a, TechId b) {
                        auto itP = prefChild.find(parent);
                        if (itP != prefChild.end()) {
                            const auto& ord = itP->second;
                            const int ra = ord.count(a) ? ord.at(a) : 100000;
                            const int rb = ord.count(b) ? ord.at(b) : 100000;
                            if (ra != rb) return ra < rb;
                        }
                        return techName(a) < techName(b);
                    };

                    for (auto& kv : children) {
                        auto& v = kv.second;
                        std::sort(v.begin(), v.end(), [&](TechId a, TechId b) { return childLess(kv.first, a, b); });
                    }

                    // Depth BFS
                    std::unordered_map<TechId, int> depth;
                    depth.reserve(techs.size() * 2 + 1);
                    std::vector<TechId> q;
                    q.reserve(techs.size());

                    std::sort(roots.begin(), roots.end(), byName);
                    for (TechId r : roots) { depth[r] = 0; q.push_back(r); }

                    for (size_t qi = 0; qi < q.size(); ++qi) {
                        TechId cur = q[qi];
                        int d = depth[cur];
                        auto it = children.find(cur);
                        if (it == children.end()) continue;
                        for (TechId ch : it->second) {
                            if (depth.count(ch)) continue;
                            depth[ch] = d + 1;
                            q.push_back(ch);
                        }
                    }

                    int maxDepth = 0;
                    for (auto& kv : depth) maxDepth = std::max(maxDepth, kv.second);
                    maxDepth = std::max(1, maxDepth);

                    // Root angles
                    // Place the 5 root technologies exactly every 360/5 degrees.
                    // SFML: +x right, +y down. Angle 0 -> right, -PI/2 -> up.
                    constexpr float PI = 3.14159265f;
                    auto normAngle = [&](float a) { while (a <= -PI) a += 2.f * PI; while (a > PI) a -= 2.f * PI; return a; };

                    const float rootStep = (2.f * PI) / 5.f;          // 360/5
                    // Shift so Organization is exactly to the right (0 rad)
                    const float startAngle = -rootStep;               // Riding is one step CCW from right

                    // Desired clockwise order around the center.
                    // With 72° spacing this makes Hunting land in the upper-left sector.
                    const std::array<TechId, 5> rootOrder = {
                        TechId::Riding,
                        TechId::Organization,
                        TechId::Climbing,
                        TechId::Fishing,
                        TechId::Hunting
                    };

                    std::unordered_map<TechId, float> preferredRootAngle;
                    preferredRootAngle.reserve(8);
                    for (size_t i = 0; i < rootOrder.size(); ++i) {
                        preferredRootAngle[rootOrder[i]] = normAngle(startAngle + float(i) * rootStep);
                    }

                    struct RootA { TechId t; float a; };
                    std::vector<RootA> rootA;
                    std::vector<TechId> unknown;
                    rootA.reserve(roots.size());
                    unknown.reserve(roots.size());

                    for (TechId r : roots) {
                        auto it = preferredRootAngle.find(r);
                        if (it != preferredRootAngle.end()) {
                            rootA.push_back({r, it->second});
                        } else {
                            unknown.push_back(r);
                        }
                    }

                    // Any extra roots (shouldn't happen for the base Polytopia tree) are spread evenly.
                    if (!unknown.empty()) {
                        std::sort(unknown.begin(), unknown.end(), byName);
                        const float step = (2.f * PI) / float(unknown.size());
                        for (size_t i = 0; i < unknown.size(); ++i) {
                            rootA.push_back({unknown[i], normAngle(startAngle + float(i) * step)});
                        }
                    }

                    std::sort(rootA.begin(), rootA.end(), [&](const RootA& a, const RootA& b) { return a.a < b.a; });

                    // Leaf counts
                    std::unordered_map<TechId, int> leafCount;
                    std::function<int(TechId)> countLeaves = [&](TechId t){
                        if (leafCount.count(t)) return leafCount[t];
                        auto it = children.find(t);
                        if (it == children.end() || it->second.empty()) return leafCount[t] = 1;
                        int sum = 0; for (TechId c : it->second) sum += std::max(1, countLeaves(c));
                        return leafCount[t] = std::max(1, sum);
                    };
                    for (auto& r : rootA) countLeaves(r.t);

                    std::unordered_map<TechId, float> angle;
                    std::function<void(TechId,float,float)> assignAngles = [&](TechId t, float a0, float a1){
                        auto it = children.find(t);
                        if (it == children.end() || it->second.empty()) { angle[t] = 0.5f*(a0+a1); return; }
                        auto ch = it->second;
                        std::sort(ch.begin(), ch.end(), [&](TechId a, TechId b){return childLess(t,a,b);});
                        int total=0; for (TechId c:ch) total+=std::max(1,leafCount[c]);
                        float cur=a0, weighted=0; int wsum=0;
                        for (TechId c:ch){ int w=std::max(1,leafCount[c]); float span=(a1-a0)*(float(w)/float(total)); float cb=cur, ce=cur+span; assignAngles(c,cb,ce); weighted+=angle[c]*float(w); wsum+=w; cur=ce; }
                        angle[t]=(wsum>0)?(weighted/float(wsum)):0.5f*(a0+a1);
                    };

                    std::unordered_map<TechId,std::pair<float,float>> rootSector;
                    const int R = (int)rootA.size();
                    const float shrink = 0.86f;
                    for (int i=0;i<R;++i){
                        float aPrev=rootA[(i-1+R)%R].a;
                        float aCur =rootA[i].a;
                        float aNext=rootA[(i+1)%R].a;
                        if (aPrev>aCur) aPrev-=2*PI;
                        if (aNext<aCur) aNext+=2*PI;
                        float left=0.5f*(aPrev+aCur);
                        float right=0.5f*(aCur+aNext);
                        float half=0.5f*(right-left)*shrink;
                        rootSector[rootA[i].t]={aCur-half,aCur+half};
                    }
                    for (auto& r:rootA){ auto it=rootSector.find(r.t); if(it!=rootSector.end()) assignAngles(r.t,it->second.first,it->second.second); else assignAngles(r.t,r.a-0.15f,r.a+0.15f);}

                    const sf::Vector2f center(left + w * 0.50f, top + h * 0.50f);
                    const float maxR = std::max(10.f, std::min(w, h) * 0.46f);
                    const float ringStep = std::max(12.f, (maxR / float(maxDepth + 1)) * 0.98f);

                    std::unordered_map<TechId, sf::Vector2f> pos;
                    buildFixedTechTreeLayout(techs, pos, center, w, h);

                    // Save tech node hitboxes for mouse clicks.
                    g_techHitValid = true;
                    g_techHitArea = sf::FloatRect(left, top, w, h);
                    g_techHitPos = pos;

                    // --------------------------
                    // DRAW EDGES
                    // --------------------------
                    {
                        sf::VertexArray lines(sf::Lines);
                        lines.clear();

                        for (TechId t : techs) {
                            const TechId pTech = TechDB::getPrerequisite(t);
                            if (pTech == TechId::Count) continue;

                            auto itA = pos.find(pTech);
                            auto itB = pos.find(t);
                            if (itA == pos.end() || itB == pos.end()) continue;

                            lines.append(sf::Vertex(itA->second, sf::Color(70, 70, 70, 220)));
                            lines.append(sf::Vertex(itB->second, sf::Color(70, 70, 70, 220)));
                        }

                        rt.draw(lines);
                    }

                    // Center head icon (focal point)
                    {
                        const TribeType tribe = curPl.getTribeType();
                        const sf::Texture& headTex = pickFirstExisting(tileCandidates(tribe, "head"));
                        const float headSize = std::clamp(ringStep * 1.25f, 26.f, 64.f);
                        drawSprite(rt, headTex,
                                   center.x - headSize * 0.5f,
                                   center.y - headSize * 0.5f,
                                   headSize);
                    }

                    // Node radius (auto-fit)
                    const float rNode = std::clamp(ringStep * 0.40f, 10.f, 18.f);
                    // Slightly larger radius for easier clicking.
                    g_techHitRadius = rNode * 1.15f;

                    const bool literacy = curPl.hasTech(TechId::Philosophy);
                    // Draw nodes + labels
                    for (TechId tech : techs) {
                        auto it = pos.find(tech);
                        if (it == pos.end()) continue;
                        const sf::Vector2f p = it->second;

                        const bool owned = curPl.hasTech(tech);
                        const int numCities = std::max(1, (int)curPl.getCities().size());
                        const int price = TechDB::calculatePrice(tech, numCities, literacy);

                        const sf::Color ownedFill(35, 140, 70, 255);
                        const sf::Color ownedOutline(90, 230, 140, 255);
                        const sf::Color missFill(120, 35, 35, 255);
                        const sf::Color missOutline(240, 90, 90, 255);

                        sf::CircleShape circ(rNode);
                        circ.setOrigin(rNode, rNode);
                        circ.setPosition(p);
                        circ.setFillColor(owned ? ownedFill : missFill);
                        circ.setOutlineThickness(3.f);
                        circ.setOutlineColor(owned ? ownedOutline : missOutline);
                        rt.draw(circ);

                        // Tech name (centered)
                        sf::Text name;
                        name.setFont(uiFont);
                        name.setCharacterSize((unsigned)std::max(9.f, rNode * 0.62f));
                        name.setFillColor(sf::Color(240, 240, 240, 255));
                        name.setString(std::string(TechDB::getTech(tech).name));

                        if (name.getString().getSize() > 12) {
                            name.setCharacterSize((unsigned)std::max(8.f, rNode * 0.52f));
                        }

                        const sf::FloatRect nb = name.getLocalBounds();
                        name.setPosition(p.x - (nb.width * 0.5f) - nb.left,
                                         p.y - (nb.height * 0.65f) - nb.top);
                        rt.draw(name);

                        // Price under name for missing techs
                        if (!owned) {
                            sf::Text pr;
                            pr.setFont(uiFont);
                            pr.setCharacterSize((unsigned)std::max(8.f, rNode * 0.50f));
                            pr.setFillColor(sf::Color(255, 210, 210, 255));
                            pr.setString(std::to_string(price));
                            const sf::FloatRect pb = pr.getLocalBounds();
                            pr.setPosition(p.x - (pb.width * 0.5f) - pb.left,
                                           p.y + (rNode * 0.15f) - pb.top);
                            rt.draw(pr);
                        }
                    }
                }
            }
        }
        else {
            const bool hasFont = ensureUIFontLoaded();

            auto addLine = [&](float& ty, const std::string& s, unsigned sz = 16) {
                if (!hasFont) return;
                sf::Text t;
                t.setFont(uiFont);
                t.setString(s);
                t.setCharacterSize(sz);
                t.setFillColor(sf::Color(240, 240, 240, 255));
                t.setPosition(panelRect.left + panelPad, ty);
                rt.draw(t);
                ty += float(sz) + 6.f;
            };

            // Only Back button in Map View
            const float bw = panelRect.width - 2.f * panelPad;
            const float bh = 40.f;
            const float baseY = panelRect.top + panelRect.height - panelPad - bh;

            btnBack = sf::FloatRect(panelRect.left + panelPad, baseY, bw, bh);
            btnEndTurn = sf::FloatRect();
            btnOverview = sf::FloatRect();

            // --- ALWAYS draw Back button in Map View (font optional) ---
            {
                sf::RectangleShape b;
                b.setPosition({btnBack.left, btnBack.top});
                b.setSize({btnBack.width, btnBack.height});
                b.setFillColor(sf::Color(35, 35, 35, 220));
                b.setOutlineThickness(2.f);
                b.setOutlineColor(sf::Color(90, 90, 90, 255));
                rt.draw(b);

                if (hasFont) {
                    sf::Text t;
                    t.setFont(uiFont);
                    t.setString("Back");
                    t.setCharacterSize(18);
                    t.setFillColor(sf::Color(240, 240, 240, 255));
                    const sf::FloatRect lb = t.getLocalBounds();
                    t.setPosition(
                        btnBack.left + (btnBack.width - lb.width) * 0.5f - lb.left,
                        btnBack.top  + (btnBack.height - lb.height) * 0.5f - lb.top - 1.f
                    );
                    rt.draw(t);
                }
            }

            // Draw Back button
            {
                sf::RectangleShape b;
                b.setPosition({btnBack.left, btnBack.top});
                b.setSize({btnBack.width, btnBack.height});
                b.setFillColor(sf::Color(35, 35, 35, 220));
                b.setOutlineThickness(2.f);
                b.setOutlineColor(sf::Color(90, 90, 90, 255));
                rt.draw(b);

                if (hasFont) {
                    sf::Text t;
                    t.setFont(uiFont);
                    t.setString("Back");
                    t.setCharacterSize(18);
                    t.setFillColor(sf::Color(240, 240, 240, 255));
                    const sf::FloatRect lb = t.getLocalBounds();
                    t.setPosition(
                        btnBack.left + (btnBack.width - lb.width) * 0.5f - lb.left,
                        btnBack.top  + (btnBack.height - lb.height) * 0.5f - lb.top - 1.f
                    );
                    rt.draw(t);
                }
            }

            // Header
            {
                float ty = panelPad + 16.f;
                addLine(ty, "Map View", 18);
                addLine(ty, "(TAB to return)", 14);
                ty += 8.f;
            }

            // Tile + unit stats (ONLY IN MAP VIEW)
            {
                float ty = panelPad + 70.f;

                addLine(ty, "===== TILE =====", 18);

                if (!selectedValid) {
                    addLine(ty, "(click a tile)", 14);
                } else {
                    const Tile& st = map.at(selectedPos);

                    addLine(ty, "Pos: (" + std::to_string(selectedPos.x) + ", " + std::to_string(selectedPos.y) + ")", 14);
                    addLine(ty, std::string("Tribe: ") + tribeDisplayName(st.getTribe()), 14);
                    addLine(ty, std::string("Terrain: ") + baseTerrainName(st.getBaseTerrain()), 14);
                    addLine(ty, std::string("Road: ") + (st.getRoadBridge() == RoadBridgeEnum::Road ? "yes" : "no"), 14);
                    addLine(ty, std::string("Settlement: ") + settlementName(st.getSettlementType()), 14);

                    // Resources list (Tile)
                    {
                        const auto resU = static_cast<uint16_t>(st.getResource());
                        auto hasR = [&](ResourcesEnum r) { return (resU & static_cast<uint16_t>(r)) != 0; };

                        std::string rline = "Resources: ";
                        bool any = false;
                        if (hasR(ResourcesEnum::Forest)) { rline += "Forest "; any = true; }
                        if (hasR(ResourcesEnum::Fruit))  { rline += "Fruit ";  any = true; }
                        if (hasR(ResourcesEnum::Crops))  { rline += "Crops ";  any = true; }
                        if (hasR(ResourcesEnum::Fish))   { rline += "Fish ";   any = true; }
                        if (hasR(ResourcesEnum::Metal))  { rline += "Metal ";  any = true; }
                        if (!any) rline += "None";
                        addLine(ty, rline, 14);
                    }

                    if (st.getSettlementType() == SettlementTypeEnum::City) {
                        addLine(ty, "===== CITY =====", 18);
                        const City* cObj = resolveCityForTile(game, st);
                        if (cObj) {
                            addLine(ty, "City name: " + cObj->getName(), 14);
                            addLine(ty, "CityId: " + std::to_string(int(cObj->getCityId())), 14);
                            addLine(ty, "OwnerId: " + std::to_string(int(cObj->getOwnerId())), 14);
                            addLine(ty, std::string("Is capital: ") + (cObj->isCapitalCity() ? "yes" : "no"), 14);
                            addLine(ty, "Level: " + std::to_string(int(cObj->getLevel())), 14);
                            addLine(ty, "Population: " + std::to_string(int(cObj->getPopulation())) + "/" + std::to_string(int(cObj->populationNeededToLevelUp())), 14);
                            addLine(ty, "Stars/round: " + std::to_string(int(cObj->getStarsPerRound())), 14);
                            addLine(ty, "Units in city: " + std::to_string(int(cObj->getUnitsCount())) + "/" + std::to_string(int(cObj->maxUnitCapacity())), 14);
                            addLine(ty, std::string("Has workshop: ") + (cObj->hasWorkshopEnabled() ? "yes" : "no"), 14);
                            addLine(ty, std::string("Has city wall: ") + (cObj->hasCityWallEnabled() ? "yes" : "no"), 14);
                        } else {
                            addLine(ty, "(City object not resolved)", 14);
                        }
                    }

                    // Unit info (if any)
                    {
                        addLine(ty, "===== UNIT =====", 18);
                        const uint32_t skey = (uint32_t(selectedPos.y) << 16u) | uint32_t(selectedPos.x);
                        auto itU = unitsByPos.find(skey);
                        if (itU != unitsByPos.end() && !itU->second.empty()) {
                            const Unit* u = itU->second.front();
                            addLine(ty, std::string("Unit: ") + unitDisplayString(*u), 14);
                            addLine(ty, "Owner: " + std::to_string(int(u->getOwnerId())), 14);
                            addLine(ty, "HP: " + std::to_string(u->getHealth()) + "/" + std::to_string(u->getMaxHealth()), 14);
                            if (itU->second.size() > 1) {
                                addLine(ty, "(+" + std::to_string(itU->second.size() - 1) + " more)", 14);
                            }
                        } else {
                            addLine(ty, "Unit: none", 14);
                        }
                    }
                }
            }
        }
        // --- Left spawn panel (gameplay view only) ---
        // Moved: now rendered after map, context bar, and right panel to always appear on top.
        if (!showOverview) {
            sf::RectangleShape lp;
            lp.setPosition({0.f, 0.f});
            lp.setSize({kLeftSpawnPanelW, float(rtSzPanel.y)});
            lp.setFillColor(sf::Color(18, 18, 18, 235));
            lp.setOutlineThickness(1.f);
            lp.setOutlineColor(sf::Color(70, 70, 70, 220));
            rt.draw(lp);

            if (ensureUIFontLoaded()) {
                sf::Text t;
                t.setFont(uiFont);
                t.setCharacterSize(16);
                t.setFillColor(sf::Color(235, 235, 235, 255));
                t.setString("SPAWN");
                t.setPosition(10.f, 8.f);
                rt.draw(t);
            }

            g_spawnHits.clear();
            g_spawnHits.reserve(32);
            g_spawnHitValid = true;

            static const std::array<UnitType, 19> kSpawnList = {
                UnitType::Warrior,
                UnitType::Archer,
                UnitType::Defender,
                UnitType::Rider,
                UnitType::MindBender,
                UnitType::Swordsman,
                UnitType::Catapult,
                UnitType::Cloak,
                UnitType::Knight,
                UnitType::Giant,
                UnitType::Bunny,
                UnitType::Bunta,
                UnitType::Raft,
                UnitType::Scout,
                UnitType::Rammer,
                UnitType::Bomber,
                UnitType::Dinghy,
                UnitType::Pirate,
                UnitType::Juggernaut
            };

            const float padX = 10.f;
            const float padY = 36.f;
            const float icon = 64.f;
            const float gap = 8.f;
            const int cols = 2;

            const TribeType iconTribe = game->getPlayer(curPid).getTribeType();

            for (size_t i = 0; i < kSpawnList.size(); ++i) {
                const int col = int(i) % cols;
                const int row = int(i) / cols;

                const float x0 = padX + float(col) * (icon + gap);
                const float y0 = padY + float(row) * (icon + gap);

                const sf::FloatRect r(x0, y0, icon, icon);
                g_spawnHits.push_back(SpawnIconHit{r, kSpawnList[i]});

                // selection highlight
                if (kSpawnList[i] == g_spawnSelectedType) {
                    sf::RectangleShape sel;
                    sel.setPosition({r.left - 2.f, r.top - 2.f});
                    sel.setSize({r.width + 4.f, r.height + 4.f});
                    sel.setFillColor(sf::Color(0,0,0,0));
                    sel.setOutlineThickness(2.f);
                    sel.setOutlineColor(sf::Color(240, 240, 240, 255));
                    rt.draw(sel);
                }

                sf::RectangleShape bg;
                bg.setPosition({r.left, r.top});
                bg.setSize({r.width, r.height});
                bg.setFillColor(sf::Color(28, 28, 28, 235));
                bg.setOutlineThickness(1.f);
                bg.setOutlineColor(sf::Color(90, 90, 90, 255));
                rt.draw(bg);

                const sf::Texture& utex = unitTexture(iconTribe, kSpawnList[i]);
                drawSprite(rt, utex, r.left, r.top, icon);
            }
        } else {
            g_spawnHitValid = false;
            g_spawnHits.clear();
            g_spawnSelectedType = UnitType::Unknown;
        }
    }
}
