#include "TechSystem.h"
#include "../game/Game.h"
#include "systems/PlayerSystem.h"
#include "systems/MonumentSystem.h"

bool TechSystem::hasTech(const Game& game, PlayerId pid, TechId tech) {
    return PlayerSystem::hasTech(game, pid, tech);
}

const std::vector<TechId>& TechSystem::getTechs(const Game& game, PlayerId pid) {
    return PlayerSystem::getTechs(game, pid);
}

bool TechSystem::canBuyTech(const Game& game, PlayerId pid, TechId tech) {
    if (pid == kNoPlayer) return false;
    if (static_cast<size_t>(pid) >= game.getPlayers().size()) return false;

    if (tech == TechId::Count) return false;

    if (PlayerSystem::hasTech(game, pid, tech)) return false;

    // prerequisite:
    // - Tier1 can be bought directly
    // - Tier2/Tier3 must have a valid prerequisite tech and that tech must be owned
    const TechData& techData = TechDB::getTech(tech);
    const TechId prereq = TechDB::getPrerequisite(tech);
    if (techData.tier != TechTier::Tier1 && prereq == TechId::Count) return false;
    if (prereq != TechId::Count && !PlayerSystem::hasTech(game, pid, prereq)) return false;

    // cena
    const int cityCount = static_cast<int>(PlayerSystem::getCities(game, pid).size());
    const bool hasLiteracy = PlayerSystem::hasTech(game, pid, TechId::Philosophy);
    const int price = TechDB::calculatePrice(tech, cityCount, hasLiteracy);

    return PlayerSystem::getStars(game, pid) >= price;
}

bool TechSystem::buyTech(Game& game, PlayerId pid, TechId tech) {
    if (!canBuyTech(game, pid, tech)) return false;

    const int cityCount = static_cast<int>(PlayerSystem::getCities(game, pid).size());
    const bool hasLiteracy = PlayerSystem::hasTech(game, pid, TechId::Philosophy);
    const int price = TechDB::calculatePrice(tech, cityCount, hasLiteracy);

    if (!PlayerSystem::spendStars(game, pid, price)) return false;

    PlayerSystem::addTech(game, pid, tech);

    // Monument: TowerOfWisdom za wszystkie techy
    bool allTechUnlocked = true;
    for (uint8_t i = 0; i < TechDB::TECH_COUNT; ++i) {
        const TechId tid = static_cast<TechId>(i);
        if (tid == TechId::Count) break;
        if (!PlayerSystem::hasTech(game, pid, tid)) { allTechUnlocked = false; break; }
    }
    MonumentSystem::onAllTechUnlockedUpdated(game, pid, allTechUnlocked);

    return true;
}
