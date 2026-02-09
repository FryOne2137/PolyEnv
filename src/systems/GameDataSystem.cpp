//
// Created by Fryderyk Niedzwiecki on 08/02/2026.
//

#include "GameDataSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <stdexcept>

#include "../content/units/Unit.h"

using json = nlohmann::json;

// Internal storage for unit templates loaded from Units.json
static std::unordered_map<UnitType, Unit> g_unitTemplates;
static bool g_unitsLoaded = false;

static UnitType unitTypeFromString(const std::string& s) {
    // Keep this in sync with Units.json keys.
    if (s == "Warrior") return UnitType::Warrior;
    if (s == "Archer") return UnitType::Archer;
    if (s == "Defender") return UnitType::Defender;
    if (s == "Rider") return UnitType::Rider;
    if (s == "Cloak") return UnitType::Cloak;
    if (s == "MindBender") return UnitType::MindBender;
    if (s == "Swordsman") return UnitType::Swordsman;
    if (s == "Catapult") return UnitType::Catapult;
    if (s == "Knight") return UnitType::Knight;
    if (s == "Dagger") return UnitType::Dagger;
    if (s == "Giant") return UnitType::Giant;
    if (s == "Bunny") return UnitType::Bunny;
    if (s == "Bunta") return UnitType::Bunta;
    if (s == "Raft") return UnitType::Raft;
    if (s == "Scout") return UnitType::Scout;
    if (s == "Rammer") return UnitType::Rammer;
    if (s == "Bomber") return UnitType::Bomber;
    if (s == "Dinghy") return UnitType::Dinghy;
    if (s == "Pirate") return UnitType::Pirate;
    if (s == "Juggernaut") return UnitType::Juggernaut;
    if (s == "Mermaid") return UnitType::Mermaid;
    if (s == "AquaticAmphibian") return UnitType::AquaticAmphibian;
    if (s == "MermaidArcher") return UnitType::MermaidArcher;
    if (s == "MermaidDefender") return UnitType::MermaidDefender;
    if (s == "Swordsmaid") return UnitType::Swordsmaid;
    if (s == "Scuba") return UnitType::Scuba;
    if (s == "Siren") return UnitType::Siren;
    if (s == "Shark") return UnitType::Shark;
    if (s == "YellyBelly") return UnitType::YellyBelly;
    if (s == "Puffer") return UnitType::Puffer;
    if (s == "TridentionAq") return UnitType::TridentionAq;
    if (s == "CrabAq") return UnitType::CrabAq;
    if (s == "Polytaur") return UnitType::Polytaur;
    if (s == "DragonEgg") return UnitType::DragonEgg;
    if (s == "BabyDragon") return UnitType::BabyDragon;
    if (s == "FireDragon") return UnitType::FireDragon;
    if (s == "IceArcher") return UnitType::IceArcher;
    if (s == "BattleSled") return UnitType::BattleSled;
    if (s == "Mooni") return UnitType::Mooni;
    if (s == "IceFortress") return UnitType::IceFortress;
    if (s == "Gaami") return UnitType::Gaami;
    if (s == "Hexapod") return UnitType::Hexapod;
    if (s == "Kiton") return UnitType::Kiton;
    if (s == "Phychi") return UnitType::Phychi;
    if (s == "Shaman") return UnitType::Shaman;
    if (s == "Raychi") return UnitType::Raychi;
    if (s == "Exida") return UnitType::Exida;
    if (s == "Doomux") return UnitType::Doomux;
    if (s == "MothC") return UnitType::MothC;
    if (s == "LarvaC") return UnitType::LarvaC;
    if (s == "InsectEgg") return UnitType::InsectEgg;
    if (s == "Boomchi") return UnitType::Boomchi;
    if (s == "LivingIsland") return UnitType::LivingIsland;
    throw std::runtime_error("GameDataSystem: unknown UnitType name in Units.json: " + s);
}

static TechId techIdFromStringOrNone(const std::string& s) {
    if (s.empty()) return TechId::Count;
    if (s == "Archery") return TechId::Archery;
    if (s == "Strategy") return TechId::Strategy;
    if (s == "Riding") return TechId::Riding;
    if (s == "Diplomacy") return TechId::Diplomacy;
    if (s == "Philosophy") return TechId::Philosophy;
    if (s == "Smithery") return TechId::Smithery;
    if (s == "Mathematics") return TechId::Mathematics;
    if (s == "Chivalry") return TechId::Chivalry;
    if (s == "Fishing") return TechId::Fishing;
    if (s == "Sailing") return TechId::Sailing;
    if (s == "Ramming") return TechId::Ramming;
    if (s == "Navigation") return TechId::Navigation;
    throw std::runtime_error("GameDataSystem: unknown TechId name in Units.json: " + s);
}

static UnitSkill unitSkillFromString(const std::string& s) {
    if (s == "Dash") return UnitSkill::Dash;
    if (s == "Fortify") return UnitSkill::Fortify;
    if (s == "Escape") return UnitSkill::Escape;
    if (s == "Hide") return UnitSkill::Hide;
    if (s == "Creep") return UnitSkill::Creep;
    if (s == "Infiltrate") return UnitSkill::Infiltrate;
    if (s == "Stiff") return UnitSkill::Stiff;
    if (s == "Scout") return UnitSkill::Scout;
    if (s == "StaticSkill") return UnitSkill::StaticSkill;
    if (s == "Heal") return UnitSkill::Heal;
    if (s == "Convert") return UnitSkill::Convert;
    if (s == "Surprise") return UnitSkill::Surprise;
    if (s == "Independent") return UnitSkill::Independent;
    if (s == "WaterOnly") return UnitSkill::WaterOnly;
    if (s == "Carry") return UnitSkill::Carry;
    if (s == "Splash") return UnitSkill::Splash;
    if (s == "Stomp") return UnitSkill::Stomp;
    if (s == "Amphibious") return UnitSkill::Amphibious;
    if (s == "Tentacles") return UnitSkill::Tentacles;
    if (s == "Drench") return UnitSkill::Drench;
    if (s == "Persist") return UnitSkill::Persist;
    if (s == "Grow") return UnitSkill::Grow;
    if (s == "Air") return UnitSkill::Air;
    if (s == "Freeze") return UnitSkill::Freeze;
    if (s == "Skate") return UnitSkill::Skate;
    if (s == "AutoFreeze") return UnitSkill::AutoFreeze;
    if (s == "FreezeArea") return UnitSkill::FreezeArea;
    if (s == "Poison") return UnitSkill::Poison;
    if (s == "Sneak") return UnitSkill::Sneak;
    if (s == "DoubleAttack") return UnitSkill::DoubleAttack;
    if (s == "Swarm") return UnitSkill::Swarm;
    if (s == "Explode") return UnitSkill::Explode;
    if (s == "Algae") return UnitSkill::Algae;
    if (s == "AutoFlood") return UnitSkill::AutoFlood;
    throw std::runtime_error("GameDataSystem: unknown UnitSkill name in Units.json: " + s);
}

static void applyJsonToUnit(Unit& out, const json& j) {
    if (j.contains("maxHealth")) out.setMaxHealth(j["maxHealth"].get<int>());
    // Health of template is kept consistent with maxHealth.
    out.setHealth(out.getMaxHealth());

    if (j.contains("attack")) out.setAttack(j["attack"].get<float>());
    if (j.contains("defense")) out.setDefense(j["defense"].get<float>());
    if (j.contains("movePoints")) out.setMovePoints(j["movePoints"].get<int>());
    if (j.contains("range")) out.setRange(j["range"].get<int>());
    if (j.contains("cost")) out.setCost(j["cost"].get<int>());
    if (j.contains("visionRange")) out.setVisionRange(j["visionRange"].get<int>());

    if (j.contains("requiredTechToSpawn") && !j["requiredTechToSpawn"].is_null()) {
        out.setRequiredTechToSpawn(techIdFromStringOrNone(j["requiredTechToSpawn"].get<std::string>()));
    } else {
        out.setRequiredTechToSpawn(TechId::Count);
    }

    if (j.contains("skills")) {
        for (const auto& s : j["skills"]) {
            out.addSkill(unitSkillFromString(s.get<std::string>()));
        }
    }

    // Keep the old invariant: Scout skill implies vision >= 2.
    if (out.hasSkill(UnitSkill::Scout)) {
        out.setVisionRange(std::max(out.getVisionRange(), 2));
    }
}

void GameDataSystem::loadUnits(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("GameDataSystem: failed to open Units.json at " + path);
    }

    json root;
    f >> root;

    if (!root.contains("units") || !root["units"].is_object()) {
        throw std::runtime_error("GameDataSystem: Units.json missing 'units' object");
    }

    // Build default template (compile-time defaults + optional _defaults override)
    Unit defaults;
    defaults.setType(UnitType::Unknown);
    defaults.setMaxHealth(10);
    defaults.setHealth(10);
    defaults.setAttack(1.0f);
    defaults.setDefense(1.0f);
    defaults.setMovePoints(1);
    defaults.setRange(1);
    defaults.setCost(0);
    defaults.setVisionRange(1);
    defaults.setRequiredTechToSpawn(TechId::Count);

    if (root.contains("_defaults") && root["_defaults"].is_object()) {
        applyJsonToUnit(defaults, root["_defaults"]);
    }

    g_unitTemplates.clear();
    for (auto& [unitName, unitJson] : root["units"].items()) {
        UnitType t = unitTypeFromString(unitName);

        Unit templ = defaults;
        templ.setType(t);

        applyJsonToUnit(templ, unitJson);

        g_unitTemplates[t] = templ;
    }

    g_unitsLoaded = true;
}

const Unit& GameDataSystem::getUnitTemplate(UnitType type) {
    if (!g_unitsLoaded) {
        throw std::runtime_error("GameDataSystem: Units.json not loaded. Call loadUnits() first.");
    }
    auto it = g_unitTemplates.find(type);
    if (it == g_unitTemplates.end()) {
        throw std::runtime_error("GameDataSystem: missing unit template for requested type");
    }
    return it->second;
}

void GameDataSystem::applyUnitTemplate(Unit& u) {
    const Unit& templ = getUnitTemplate(u.getType());

    // Preserve runtime identity/position/turn state.
    const UnitId id = u.getId();
    const PlayerId owner = u.getOwnerId();
    const Pos pos = u.getPos();
    const bool moved = u.movedThisTurn();
    const bool attacked = u.attackedThisTurn();
    const bool veteran = u.isVeteran();
    const bool poisoned = u.poisoned();
    const int kills = u.getKillCounter();
    const UnitType embarkedBase = u.getEmbarkedBaseType();
    const bool embarked = u.isEmbarked();

    // Overwrite with template (includes skills/cost/stats/etc.).
    u = templ;

    // Restore preserved runtime fields.
    u.setId(id);
    u.setOwnerId(owner);
    u.setPos(pos);
    u.setMovedThisTurn(moved);
    u.setAttackedThisTurn(attacked);
    u.setVeteran(veteran);
    u.setPoisoned(poisoned);
    u.setKillCounter(kills);
    if (embarked) {
        u.setEmbarkedBaseType(embarkedBase);
    } else {
        u.clearEmbarkedBaseType();
    }
}

int GameDataSystem::getUnitCost(UnitType type) {
    return std::max(0, getUnitTemplate(type).getCost());
}