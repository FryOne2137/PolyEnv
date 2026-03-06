//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "TribeSelectScreen.h"

#include <algorithm>
#include <cmath>
#include "TextureStore.h"

// ── file-static input-box state ──────────────────────────────────────────────
static bool mapSizeActive  = false;
static std::string mapSizeBuffer = "16";

static bool seedActive     = false;
static std::string seedBuffer    = "0";

static int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

// ── Layout constants ──────────────────────────────────────────────────────────
// Left tribe list
static constexpr float kListX   = 40.f;
static constexpr float kListY   = 100.f;
static constexpr float kRowH    = 28.f;

// Selected panel
static constexpr float kSelX    = 420.f;
static constexpr float kSelY    = 130.f;
static constexpr float kSelNameW = 280.f;  // name portion of each row
static constexpr float kBtnW    = 44.f;    // width of [P] / [B] button
static constexpr float kBtnGap  = 4.f;

// Port input box — same row as "Count:", to the right of it
static constexpr float kPortLabelX = 960.f;
static constexpr float kPortLabelY = 610.f;
static constexpr float kPortBoxX   = 1010.f;
static constexpr float kPortBoxY   = 603.f;
static constexpr float kPortBoxW   = 120.f;
static constexpr float kPortBoxH   = 28.f;

// ── helpers ───────────────────────────────────────────────────────────────────
static bool inside(const sf::FloatRect& r, sf::Vector2f p) { return r.contains(p); }

// ── TribeSelectScreen implementation ──────────────────────────────────────────

const char* TribeSelectScreen::tribeFolder(TribeType t) {
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

TribeSelectScreen::TribeSelectScreen() {
    allTribes = {
        TribeType::XinXi, TribeType::Imperius, TribeType::Bardur, TribeType::Oumaji,
        TribeType::Kickoo, TribeType::Hoodrick, TribeType::Luxidoor, TribeType::Vengir,
        TribeType::Zebasi, TribeType::AiMo, TribeType::Quetzali, TribeType::Yadakk,
        TribeType::Aquarion, TribeType::Elyrion, TribeType::Polaris, TribeType::Cymanti
    };

    startBtn = { sf::FloatRect(40, 600, 240, 50), "Start" };
    popBtn   = { sf::FloatRect(300, 600, 240, 50), "Remove last" };
    clearBtn = { sf::FloatRect(560, 600, 240, 50), "Clear" };

    // default 4 players (all human)
    selected     = { TribeType::XinXi, TribeType::Kickoo, TribeType::Luxidoor, TribeType::Oumaji };
    selectedBots = { false, false, false, false };

    textureStore = &TextureStore::instance();

    mapSize = 16;
    mapSeed = 0;
    initialLand  = 50;
    smoothing    = 50;
    relief       = 50;
    activeSlider = ActiveSlider::None;
}

void TribeSelectScreen::setFont(sf::Font* f) { font = f; }

const std::vector<TribeType>& TribeSelectScreen::getSelectedTribes() const { return selected; }
const std::vector<bool>&      TribeSelectScreen::getSelectedBots()   const { return selectedBots; }
int TribeSelectScreen::getMapSize()    const { return mapSize; }
int TribeSelectScreen::getMapSeed()    const { return mapSeed; }
int TribeSelectScreen::getInitialLand() const { return initialLand; }
int TribeSelectScreen::getSmoothing()  const { return smoothing; }
int TribeSelectScreen::getRelief()     const { return relief; }

int TribeSelectScreen::getServerPort() const {
    if (portBuffer.empty()) return 5555;
    try { return std::stoi(portBuffer); } catch (...) { return 5555; }
}

bool TribeSelectScreen::consumeStartClicked() {
    const bool v = startClicked;
    startClicked = false;
    return v;
}

// ── Event handling ────────────────────────────────────────────────────────────

void TribeSelectScreen::handleEvent(const sf::Event& ev, const sf::RenderWindow& window) {
    // Slider geometry
    const float sliderX = 840.f;
    const float sliderW = 360.f;
    const float sliderH = 18.f;
    const sf::FloatRect initialLandTrack(sliderX, 140.f, sliderW, sliderH);
    const sf::FloatRect smoothingTrack  (sliderX, 210.f, sliderW, sliderH);
    const sf::FloatRect reliefTrack     (sliderX, 280.f, sliderW, sliderH);

    auto sliderValueFromMouse = [&](const sf::FloatRect& track, sf::Vector2f mp) -> int {
        const float t = (mp.x - track.left) / track.width;
        return clampInt(static_cast<int>(std::round(t * 100.f)), 0, 100);
    };

    // ── MouseMoved ────────────────────────────────────────────────────────────
    if (ev.type == sf::Event::MouseMoved) {
        const sf::Vector2f mp = window.mapPixelToCoords({ev.mouseMove.x, ev.mouseMove.y});
        hoverIndex = -1;

        // Drag active slider
        if (sf::Mouse::isButtonPressed(sf::Mouse::Left) && activeSlider != ActiveSlider::None) {
            if (activeSlider == ActiveSlider::InitialLand)
                initialLand = sliderValueFromMouse(initialLandTrack, mp);
            else if (activeSlider == ActiveSlider::Smoothing)
                smoothing = sliderValueFromMouse(smoothingTrack, mp);
            else if (activeSlider == ActiveSlider::Relief)
                relief = sliderValueFromMouse(reliefTrack, mp);
        }

        // Tribe list hover
        for (int i = 0; i < (int)allTribes.size(); ++i) {
            sf::FloatRect rr(kListX, kListY + i * kRowH, 300.f, kRowH);
            if (inside(rr, mp)) hoverIndex = i;
        }
    }

    // ── MouseButtonPressed ────────────────────────────────────────────────────
    if (ev.type == sf::Event::MouseButtonPressed &&
        ev.mouseButton.button == sf::Mouse::Left) {

        const sf::Vector2f mp =
            window.mapPixelToCoords({ev.mouseButton.x, ev.mouseButton.y});

        // Action buttons
        if (inside(startBtn.rect, mp)) { startClicked = true; return; }
        if (inside(popBtn.rect, mp)) {
            if (!selected.empty()) { selected.pop_back(); selectedBots.pop_back(); }
            return;
        }
        if (inside(clearBtn.rect, mp)) { selected.clear(); selectedBots.clear(); return; }

        // Sliders
        if (inside(initialLandTrack, mp)) {
            activeSlider = ActiveSlider::InitialLand;
            initialLand  = sliderValueFromMouse(initialLandTrack, mp);
            return;
        }
        if (inside(smoothingTrack, mp)) {
            activeSlider = ActiveSlider::Smoothing;
            smoothing    = sliderValueFromMouse(smoothingTrack, mp);
            return;
        }
        if (inside(reliefTrack, mp)) {
            activeSlider = ActiveSlider::Relief;
            relief       = sliderValueFromMouse(reliefTrack, mp);
            return;
        }
        activeSlider = ActiveSlider::None;

        // Map size / seed input boxes
        sf::FloatRect mapSizeBox(900.f, 550.f, 140.f, 40.f);
        sf::FloatRect seedBox   (1100.f, 550.f, 140.f, 40.f);
        sf::FloatRect portBox   (kPortBoxX, kPortBoxY, kPortBoxW, kPortBoxH);

        if (inside(mapSizeBox, mp)) { mapSizeActive = true; seedActive = false; portActive = false; return; }
        if (inside(seedBox, mp))    { seedActive = true;    mapSizeActive = false; portActive = false; return; }
        if (inside(portBox, mp))    { portActive = true;    mapSizeActive = false; seedActive = false; return; }

        mapSizeActive = false;
        seedActive    = false;
        portActive    = false;

        // ── P / B toggle buttons in the selected panel ────────────────────
        for (int i = 0; i < (int)selected.size(); ++i) {
            const float rowY = kSelY + i * kRowH;

            // [P] button
            sf::FloatRect pBtn(kSelX + kSelNameW + kBtnGap,
                               rowY + 1.f, kBtnW, kRowH - 4.f);
            if (inside(pBtn, mp)) {
                selectedBots[i] = false;
                return;
            }

            // [B] button
            sf::FloatRect bBtn(kSelX + kSelNameW + kBtnGap + kBtnW + kBtnGap,
                               rowY + 1.f, kBtnW, kRowH - 4.f);
            if (inside(bBtn, mp)) {
                selectedBots[i] = true;
                return;
            }
        }

        // ── Add tribe to selected list ────────────────────────────────────
        if (mp.x >= kListX && mp.x <= kListX + 320.f && mp.y >= kListY) {
            int index = static_cast<int>((mp.y - kListY) / kRowH);
            if (index >= 0 && index < (int)allTribes.size() && selected.size() < 16) {
                selected.push_back(allTribes[index]);
                selectedBots.push_back(false); // default: human
                return;
            }
        }
    }

    // ── MouseButtonReleased ───────────────────────────────────────────────────
    if (ev.type == sf::Event::MouseButtonReleased &&
        ev.mouseButton.button == sf::Mouse::Left) {
        activeSlider = ActiveSlider::None;
    }

    // ── TextEntered ───────────────────────────────────────────────────────────
    if (ev.type == sf::Event::TextEntered) {
        const bool anyActive = mapSizeActive || seedActive || portActive;
        if (!anyActive) return;

        std::string& buf = mapSizeActive ? mapSizeBuffer
                         : seedActive    ? seedBuffer
                                         : portBuffer;

        if (ev.text.unicode >= '0' && ev.text.unicode <= '9') {
            if (buf.size() < 12)
                buf.push_back(static_cast<char>(ev.text.unicode));
        } else if (ev.text.unicode == '-' && seedActive && buf.empty()) {
            buf.push_back('-');
        } else if (ev.text.unicode == 8 && !buf.empty()) { // backspace
            buf.pop_back();
        }

        // Apply values
        if (mapSizeActive && !mapSizeBuffer.empty()) {
            try {
                int v = std::stoi(mapSizeBuffer);
                if (v >= 11) mapSize = v;
            } catch (...) {}
        } else if (seedActive && !seedBuffer.empty() && seedBuffer != "-") {
            try { mapSeed = std::stoi(seedBuffer); } catch (...) {}
        }
        // portBuffer is read lazily via getServerPort()
    }
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void TribeSelectScreen::draw(sf::RenderTarget& rt) {
    // Background panel
    sf::RectangleShape panel;
    panel.setFillColor(sf::Color(30, 30, 30));
    panel.setOutlineThickness(1);
    panel.setOutlineColor(sf::Color(60, 60, 60));
    panel.setPosition(20, 20);
    panel.setSize({1240, 680});
    rt.draw(panel);

    // Title
    if (font) {
        sf::Text title("Select tribes (duplicates allowed). Click tribe to add.", *font, 22);
        title.setPosition(40, 35);
        rt.draw(title);

        sf::Text hint("Need 2..16 tribes. Start will create players in this exact pick order.", *font, 16);
        hint.setPosition(40, 60);
        hint.setFillColor(sf::Color(180, 180, 180));
        rt.draw(hint);
    }

    // ── Left: tribe list ──────────────────────────────────────────────────────
    for (int i = 0; i < (int)allTribes.size(); ++i) {
        sf::RectangleShape row;
        row.setPosition(kListX, kListY + i * kRowH);
        row.setSize({320.f, kRowH - 2.f});
        row.setFillColor(i == hoverIndex ? sf::Color(60, 60, 80) : sf::Color(45, 45, 45));
        rt.draw(row);

        if (textureStore) {
            const char* folder = tribeFolder(allTribes[i]);
            std::string path = std::string("assets/textures/Polytopia_game_engine_textures/tribes/")
                               + folder + "/head.png";
            const sf::Texture& tex = textureStore->get(path);
            sf::Sprite spr(tex);
            float scale = (kRowH - 6.f) / tex.getSize().y;
            spr.setScale(scale, scale);
            spr.setPosition(kListX + 4.f, kListY + i * kRowH + 3.f);
            rt.draw(spr);
        }

        if (font) {
            sf::Text t(tribeName(allTribes[i]), *font, 16);
            t.setPosition(kListX + kRowH + 8.f, kListY + i * kRowH + 4.f);
            rt.draw(t);
        }
    }

    // ── Middle: selected list ─────────────────────────────────────────────────
    if (font) {
        sf::Text selTitle("Selected (pick order):", *font, 18);
        selTitle.setPosition(kSelX, 100.f);
        rt.draw(selTitle);

        // Column headers for P/B buttons
        sf::Text ph("P", *font, 13);
        ph.setFillColor(sf::Color(140, 200, 140));
        ph.setPosition(kSelX + kSelNameW + kBtnGap + kBtnW * 0.35f, 113.f);
        rt.draw(ph);

        sf::Text bh("B", *font, 13);
        bh.setFillColor(sf::Color(200, 140, 140));
        bh.setPosition(kSelX + kSelNameW + kBtnGap + kBtnW + kBtnGap + kBtnW * 0.35f, 113.f);
        rt.draw(bh);
    }

    for (int i = 0; i < (int)selected.size(); ++i) {
        const float rowY = kSelY + i * kRowH;
        const bool isBot = selectedBots[i];

        // Name background
        sf::RectangleShape row;
        row.setPosition(kSelX, rowY);
        row.setSize({kSelNameW, kRowH - 2.f});
        row.setFillColor(sf::Color(40, 40, 40));
        rt.draw(row);

        if (font) {
            sf::Text t(std::to_string(i) + ": " + tribeName(selected[i]), *font, 16);
            t.setPosition(kSelX + 8.f, rowY + 4.f);
            rt.draw(t);
        }

        // [P] button
        {
            const float bx = kSelX + kSelNameW + kBtnGap;
            sf::RectangleShape btn;
            btn.setPosition(bx, rowY + 1.f);
            btn.setSize({kBtnW, kRowH - 4.f});
            btn.setFillColor(!isBot ? sf::Color(50, 130, 50) : sf::Color(55, 55, 55));
            btn.setOutlineThickness(1);
            btn.setOutlineColor(sf::Color(100, 100, 100));
            rt.draw(btn);
            if (font) {
                sf::Text t("P", *font, 14);
                t.setFillColor(sf::Color(220, 255, 220));
                t.setPosition(bx + kBtnW * 0.3f, rowY + 4.f);
                rt.draw(t);
            }
        }

        // [B] button
        {
            const float bx = kSelX + kSelNameW + kBtnGap + kBtnW + kBtnGap;
            sf::RectangleShape btn;
            btn.setPosition(bx, rowY + 1.f);
            btn.setSize({kBtnW, kRowH - 4.f});
            btn.setFillColor(isBot ? sf::Color(130, 50, 50) : sf::Color(55, 55, 55));
            btn.setOutlineThickness(1);
            btn.setOutlineColor(sf::Color(100, 100, 100));
            rt.draw(btn);
            if (font) {
                sf::Text t("B", *font, 14);
                t.setFillColor(sf::Color(255, 220, 220));
                t.setPosition(bx + kBtnW * 0.3f, rowY + 4.f);
                rt.draw(t);
            }
        }
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto drawButton = [&](const Button& b) {
        sf::RectangleShape r;
        r.setPosition(b.rect.left, b.rect.top);
        r.setSize({b.rect.width, b.rect.height});
        r.setFillColor(sf::Color(70, 70, 70));
        r.setOutlineThickness(2);
        r.setOutlineColor(sf::Color(120, 120, 120));
        rt.draw(r);
        if (font) {
            sf::Text t(b.text, *font, 18);
            t.setPosition(b.rect.left + 14, b.rect.top + 12);
            rt.draw(t);
        }
    };
    drawButton(startBtn);
    drawButton(popBtn);
    drawButton(clearBtn);

    // ── Map size + seed input boxes ───────────────────────────────────────────
    sf::FloatRect mapSizeBox(900.f, 550.f, 140.f, 40.f);
    sf::FloatRect seedBox   (1100.f, 550.f, 140.f, 40.f);

    auto drawInputBox = [&](const sf::FloatRect& r, bool active) {
        sf::RectangleShape b;
        b.setPosition(r.left, r.top);
        b.setSize({r.width, r.height});
        b.setFillColor(active ? sf::Color(60, 60, 60) : sf::Color(40, 40, 40));
        b.setOutlineThickness(2);
        b.setOutlineColor(sf::Color(120, 120, 120));
        rt.draw(b);
    };
    drawInputBox(mapSizeBox, mapSizeActive);
    drawInputBox(seedBox, seedActive);

    if (font) {
        sf::Text msLabel("Map size (min 11):", *font, 18);
        msLabel.setPosition(840.f, 520.f);
        rt.draw(msLabel);

        sf::Text seedLabel("Seed:", *font, 18);
        seedLabel.setPosition(1100.f, 520.f);
        rt.draw(seedLabel);

        sf::Text msValue(mapSizeBuffer, *font, 20);
        msValue.setPosition(mapSizeBox.left + 10.f, mapSizeBox.top + 8.f);
        rt.draw(msValue);

        sf::Text seedValue(seedBuffer, *font, 20);
        seedValue.setPosition(seedBox.left + 10.f, seedBox.top + 8.f);
        rt.draw(seedValue);
    }

    // ── Sliders ───────────────────────────────────────────────────────────────
    auto drawSlider = [&](const std::string& label, const sf::FloatRect& track, int value) {
        if (font) {
            sf::Text l(label + ": " + std::to_string(value), *font, 18);
            l.setPosition(track.left, track.top - 28.f);
            rt.draw(l);
        }

        sf::RectangleShape tr;
        tr.setPosition(track.left, track.top);
        tr.setSize({track.width, track.height});
        tr.setFillColor(sf::Color(45, 45, 45));
        tr.setOutlineThickness(2);
        tr.setOutlineColor(sf::Color(120, 120, 120));
        rt.draw(tr);

        sf::RectangleShape fill;
        fill.setPosition(track.left, track.top);
        fill.setSize({track.width * (value / 100.f), track.height});
        fill.setFillColor(sf::Color(70, 70, 90));
        rt.draw(fill);

        const float knobX = track.left + track.width * (value / 100.f);
        sf::RectangleShape knob;
        knob.setPosition(knobX - 6.f, track.top - 4.f);
        knob.setSize({12.f, track.height + 8.f});
        knob.setFillColor(sf::Color(200, 200, 200));
        rt.draw(knob);
    };

    const float sliderX = 840.f;
    const float sliderW = 360.f;
    const float sliderH = 18.f;
    drawSlider("Initial land", sf::FloatRect(sliderX, 140.f, sliderW, sliderH), initialLand);
    drawSlider("Smoothing",    sf::FloatRect(sliderX, 210.f, sliderW, sliderH), smoothing);
    drawSlider("Relief",       sf::FloatRect(sliderX, 280.f, sliderW, sliderH), relief);

    if (font) {
        sf::Text cnt("Count: " + std::to_string(selected.size()), *font, 18);
        cnt.setPosition(840.f, 610.f);
        rt.draw(cnt);

        // Port label
        sf::Text portLbl("Port:", *font, 18);
        portLbl.setPosition(kPortLabelX, kPortLabelY);
        portLbl.setFillColor(sf::Color(200, 200, 200));
        rt.draw(portLbl);
    }

    // Port input box
    {
        sf::RectangleShape box;
        box.setPosition(kPortBoxX, kPortBoxY);
        box.setSize({kPortBoxW, kPortBoxH});
        box.setFillColor(portActive ? sf::Color(60, 60, 60) : sf::Color(40, 40, 40));
        box.setOutlineThickness(2);
        box.setOutlineColor(portActive ? sf::Color(180, 180, 100) : sf::Color(120, 120, 120));
        rt.draw(box);

        if (font) {
            sf::Text val(portBuffer, *font, 16);
            val.setPosition(kPortBoxX + 6.f, kPortBoxY + 5.f);
            rt.draw(val);
        }
    }
}

const char* TribeSelectScreen::tribeName(TribeType t) {
    switch (t) {
        case TribeType::XinXi:    return "Xin-xi";
        case TribeType::Imperius: return "Imperius";
        case TribeType::Bardur:   return "Bardur";
        case TribeType::Oumaji:   return "Oumaji";
        case TribeType::Kickoo:   return "Kickoo";
        case TribeType::Hoodrick: return "Hoodrick";
        case TribeType::Luxidoor: return "Luxidoor";
        case TribeType::Vengir:   return "Vengir";
        case TribeType::Zebasi:   return "Zebasi";
        case TribeType::AiMo:     return "Ai-mo";
        case TribeType::Quetzali: return "Quetzali";
        case TribeType::Yadakk:   return "Yadakk";
        case TribeType::Aquarion: return "Aquarion";
        case TribeType::Elyrion:  return "Elyrion";
        case TribeType::Polaris:  return "Polaris";
        case TribeType::Cymanti:  return "Cymanti";
        default: return "Unknown";
    }
}
