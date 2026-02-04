#include "TechSystem.h"
#include "Game.h"
#include "Systems/MonumentSystem.h"

bool TechSystem::hasTech(const Player& pl, TechId tech) {
    return pl.hasTech(tech);
}

const std::vector<TechId>& TechSystem::getTechs(const Player& pl) {
    return pl.getTechs();
}

bool TechSystem::canBuyTech(const Game& game, PlayerId pid, TechId tech) {
    if (pid == kNoPlayer) return false;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return false;


    if (tech == TechId::Count) return false;

    const Player& pl = game.getPlayer(pid);

    if (pl.hasTech(tech)) return false;

    // prerequisite
    const TechId prereq = TechDB::getPrerequisite(tech);
    if (prereq != TechId::Count && !pl.hasTech(prereq)) return false;

    // cena
    const int cityCount = static_cast<int>(pl.getCities().size());
    const bool hasLiteracy = pl.hasTech(TechId::Philosophy);
    const int price = TechDB::calculatePrice(tech, cityCount, hasLiteracy);

    return pl.getStars() >= price;
}

bool TechSystem::buyTech(Game& game, PlayerId pid, TechId tech) {
    if (!canBuyTech(game, pid, tech)) return false;

    Player& pl = game.getPlayer(pid);

    const int cityCount = static_cast<int>(pl.getCities().size());
    const bool hasLiteracy = pl.hasTech(TechId::Philosophy);
    const int price = TechDB::calculatePrice(tech, cityCount, hasLiteracy);

    if (!pl.spendStars(price)) return false;

    pl.addTech(tech);

    // Monument: TowerOfWisdom za wszystkie techy
    bool allTechUnlocked = true;
    for (uint8_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        const TechId tid = static_cast<TechId>(i);
        if (tid == TechId::Count) break;
        if (!pl.hasTech(tid)) { allTechUnlocked = false; break; }
    }
    MonumentSystem::onAllTechUnlockedUpdated(game, pid, allTechUnlocked);

    return true;
}