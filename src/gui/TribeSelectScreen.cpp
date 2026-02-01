//
// Created by Fryderyk Niedzwiecki on 15/01/2026.
//

#include "TribeSelectScreen.h"

#include <algorithm>
#include <cmath>
#include "TextureStore.h"

static bool mapSizeActive = false;
static std::string mapSizeBuffer = "16";

static bool seedActive = false;
static std::string seedBuffer = "0";

static int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

const char* TribeSelectScreen::tribeFolder(TribeType t) {
    // Folder names in assets/Polytopia_game_engine_textures/tribes/
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

    // domyślnie 2 graczy (możesz usunąć)
    selected = { TribeType::XinXi, TribeType::Kickoo, TribeType::Luxidoor, TribeType::Oumaji, };

    textureStore = &TextureStore::instance();

    mapSize = 16; // default map size
    mapSeed = 0; // default seed

    // Map gen sliders (0..100) like in the original repo
    initialLand  = 50;
    smoothing    = 50;
    relief       = 50;
    activeSlider = ActiveSlider::None;
}

void TribeSelectScreen::setFont(sf::Font* f) { font = f; }

const std::vector<TribeType>& TribeSelectScreen::getSelectedTribes() const { return selected; }

int TribeSelectScreen::getMapSize() const { return mapSize; }
int TribeSelectScreen::getMapSeed() const { return mapSeed; }
int TribeSelectScreen::getInitialLand() const { return initialLand; }
int TribeSelectScreen::getSmoothing() const { return smoothing; }
int TribeSelectScreen::getRelief() const { return relief; }

bool TribeSelectScreen::consumeStartClicked() {
    const bool v = startClicked;
    startClicked = false;
    return v;
}

static bool inside(const sf::FloatRect& r, sf::Vector2f p) { return r.contains(p); }

void TribeSelectScreen::handleEvent(const sf::Event& ev, const sf::RenderWindow& window) {
    // Slider geometry (shared for press/drag)
    const float sliderX = 840.f;
    const float sliderW = 360.f;
    const float sliderH = 18.f;

    const sf::FloatRect initialLandTrack(sliderX, 140.f, sliderW, sliderH);
    const sf::FloatRect smoothingTrack  (sliderX, 210.f, sliderW, sliderH);
    const sf::FloatRect reliefTrack     (sliderX, 280.f, sliderW, sliderH);

    auto sliderValueFromMouse = [&](const sf::FloatRect& track, sf::Vector2f mp) -> int {
        const float t = (mp.x - track.left) / track.width;
        const int v = (int)std::round(t * 100.f);
        return clampInt(v, 0, 100);
    };

    if (ev.type == sf::Event::MouseMoved) {
        const sf::Vector2f mp =
            window.mapPixelToCoords({ev.mouseMove.x, ev.mouseMove.y});
        hoverIndex = -1;

        // Drag active slider
        if (sf::Mouse::isButtonPressed(sf::Mouse::Left) && activeSlider != ActiveSlider::None) {
            if (activeSlider == ActiveSlider::InitialLand) {
                initialLand = sliderValueFromMouse(initialLandTrack, mp);
            } else if (activeSlider == ActiveSlider::Smoothing) {
                smoothing = sliderValueFromMouse(smoothingTrack, mp);
            } else if (activeSlider == ActiveSlider::Relief) {
                relief = sliderValueFromMouse(reliefTrack, mp);
            }
        }

        // list of tribes area
        const float leftX = 40.f;
        const float topY  = 100.f;
        const float rowH  = 28.f;
        for (int i = 0; i < (int)allTribes.size(); ++i) {
            sf::FloatRect rr(leftX, topY + i * rowH, 300.f, rowH);
            if (inside(rr, mp)) hoverIndex = i;
        }
    }

    if (ev.type == sf::Event::MouseButtonPressed &&
        ev.mouseButton.button == sf::Mouse::Left) {

        const sf::Vector2f mp =
            window.mapPixelToCoords(
                {ev.mouseButton.x, ev.mouseButton.y}
            );

        if (inside(startBtn.rect, mp)) {
            startClicked = true;
            return;
        }

        if (inside(popBtn.rect, mp)) {
            if (!selected.empty())
                selected.pop_back();
            return;
        }

        if (inside(clearBtn.rect, mp)) {
            selected.clear();
            return;
        }

        // Slider focus (click on track to set value, and enable dragging)
        if (inside(initialLandTrack, mp)) {
            activeSlider = ActiveSlider::InitialLand;
            initialLand = sliderValueFromMouse(initialLandTrack, mp);
            return;
        }
        if (inside(smoothingTrack, mp)) {
            activeSlider = ActiveSlider::Smoothing;
            smoothing = sliderValueFromMouse(smoothingTrack, mp);
            return;
        }
        if (inside(reliefTrack, mp)) {
            activeSlider = ActiveSlider::Relief;
            relief = sliderValueFromMouse(reliefTrack, mp);
            return;
        }
        activeSlider = ActiveSlider::None;

        // Map size + seed input box focus
        sf::FloatRect mapSizeBox(900.f, 550.f, 140.f, 40.f);
        sf::FloatRect seedBox   (1100.f, 550.f, 140.f, 40.f);

        if (inside(mapSizeBox, mp)) {
            mapSizeActive = true;
            seedActive = false;
            return;
        }
        if (inside(seedBox, mp)) {
            seedActive = true;
            mapSizeActive = false;
            return;
        }

        mapSizeActive = false;
        seedActive = false;

        // Determine clicked tribe index directly from mouse position
        const float leftX = 40.f;
        const float topY  = 100.f;
        const float rowH  = 28.f;

        if (mp.x >= leftX && mp.x <= leftX + 320.f &&
            mp.y >= topY) {

            int index = int((mp.y - topY) / rowH);
            if (index >= 0 && index < (int)allTribes.size()) {
                if (selected.size() < 16) {
                    selected.push_back(allTribes[index]);
                }
                return;
            }
        }
    }

    if (ev.type == sf::Event::MouseButtonReleased && ev.mouseButton.button == sf::Mouse::Left) {
        activeSlider = ActiveSlider::None;
    }

    if (ev.type == sf::Event::TextEntered && (mapSizeActive || seedActive)) {
        auto& buf = mapSizeActive ? mapSizeBuffer : seedBuffer;

        // allow digits; for seed also allow leading '-'
        if (ev.text.unicode >= '0' && ev.text.unicode <= '9') {
            if (buf.size() < 12) // enough for int32 including sign
                buf.push_back(static_cast<char>(ev.text.unicode));
        } else if (ev.text.unicode == '-' && seedActive) {
            if (buf.empty())
                buf.push_back('-');
        } else if (ev.text.unicode == 8 && !buf.empty()) { // backspace
            buf.pop_back();
        }

        // Apply value
        if (mapSizeActive) {
            if (!mapSizeBuffer.empty()) {
                try {
                    int v = std::stoi(mapSizeBuffer);
                    if (v >= 11)
                        mapSize = v;
                } catch (...) {
                    // ignore invalid partial input
                }
            }
        } else {
            if (!seedBuffer.empty() && seedBuffer != "-") {
                try {
                    mapSeed = std::stoi(seedBuffer);
                } catch (...) {
                    // ignore invalid partial input
                }
            }
        }
    }
}

void TribeSelectScreen::draw(sf::RenderTarget& rt) {
    // Background panels
    sf::RectangleShape panel;
    panel.setFillColor(sf::Color(30, 30, 30));
    panel.setOutlineThickness(1);
    panel.setOutlineColor(sf::Color(60, 60, 60));

    panel.setPosition(20, 20);
    panel.setSize({1240, 680});
    rt.draw(panel);

    // Titles
    if (font) {
        sf::Text title("Select tribes (duplicates allowed). Click tribe to add.", *font, 22);
        title.setPosition(40, 35);
        rt.draw(title);

        sf::Text hint("Need 2..16 tribes. Start will create players in this exact pick order.", *font, 16);
        hint.setPosition(40, 60);
        hint.setFillColor(sf::Color(180, 180, 180));
        rt.draw(hint);
    }

    // Tribe list left
    const float leftX = 40.f;
    const float topY  = 100.f;
    const float rowH  = 28.f;

    for (int i = 0; i < (int)allTribes.size(); ++i) {
        sf::RectangleShape row;
        row.setPosition(leftX, topY + i * rowH);
        row.setSize({320.f, rowH - 2.f});
        row.setFillColor(i == hoverIndex ? sf::Color(60, 60, 80) : sf::Color(45, 45, 45));
        rt.draw(row);

        if (textureStore) {
            const char* folder = tribeFolder(allTribes[i]);
            std::string path =
                std::string("assets/Polytopia_game_engine_textures/tribes/") + folder + "/head.png";
            const sf::Texture& tex = textureStore->get(path);
            sf::Sprite spr(tex);

            float scale = (rowH - 6.f) / tex.getSize().y;
            spr.setScale(scale, scale);
            spr.setPosition(leftX + 4.f, topY + i * rowH + 3.f);
            rt.draw(spr);
        }

        if (font) {
            sf::Text t(tribeName(allTribes[i]), *font, 16);
            t.setPosition(leftX + rowH + 8.f, topY + i * rowH + 4.f);
            rt.draw(t);
        }
    }

    // Selected list right
    const float rightX = 420.f;
    if (font) {
        sf::Text selTitle("Selected (pick order):", *font, 18);
        selTitle.setPosition(rightX, 100.f);
        rt.draw(selTitle);
    }

    for (int i = 0; i < (int)selected.size(); ++i) {
        sf::RectangleShape row;
        row.setPosition(rightX, 130.f + i * rowH);
        row.setSize({400.f, rowH - 2.f});
        row.setFillColor(sf::Color(40, 40, 40));
        rt.draw(row);

        if (font) {
            sf::Text t(std::to_string(i) + ": " + tribeName(selected[i]), *font, 16);
            t.setPosition(rightX + 8.f, 130.f + i * rowH + 4.f);
            rt.draw(t);
        }
    }

    // Buttons
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

    // Map size + seed inputs
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

    // Sliders
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