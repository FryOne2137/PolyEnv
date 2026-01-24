//
// Created by Fryderyk Niedzwiecki on 10/01/2026.
//

#include "Tribe.h"

#include <utility>
#include <string>

#include "techs/Tech.h"
#include "units/Unit.h"

Tribe::Tribe(TribeType type, std::string name, uint8_t stars, TechId tech, UnitType startUnitType)
    : tribeType(type),
      tribeName(std::move(name)),
      startStars(stars),
      startTech(tech) {
    startUnit.setType(startUnitType);
}

TribeType Tribe::getType() const {
    return tribeType;
}

uint8_t Tribe::getStartStars() const {
    return startStars;
}

const std::string& Tribe::getName() const {
    return tribeName;
}

TechId Tribe::getStartTech() const {
    return startTech;
}

const char* Tribe::defaultName(TribeType type) {
    switch (type) {
        case TribeType::XinXi:    return "Xin-xi";
        case TribeType::Imperius: return "Imperius";
        case TribeType::Bardur:   return "Bardur";
        case TribeType::Oumaji:   return "Oumaji";
        case TribeType::Kickoo:   return "Kickoo";
        case TribeType::Hoodrick: return "Hoodrick";
        case TribeType::Luxidoor: return "Luxidoor";
        case TribeType::Vengir:   return "Vengir";
        case TribeType::Zebasi:   return "Zebasi";
        case TribeType::AiMo:     return "Ai-Mo";
        case TribeType::Quetzali: return "Quetzali";
        case TribeType::Yadakk:   return "Yădakk";
        case TribeType::Aquarion: return "Aquarion";
        case TribeType::Elyrion:  return "Elyrion";
        case TribeType::Polaris:  return "Polaris";
        case TribeType::Cymanti:  return "Cymanti";
        default:                  return "Unknown";
    }
}

uint8_t Tribe::defaultStartStars(TribeType type) {
    // Wiki lists starting stars; Luxidoor is the special case with very low starting stars.
    // If you later add game-mode modifiers, do it outside of Tribe.
    switch (type) {
        case TribeType::XinXi: return 7;
        case TribeType::Oumaji: return 6;
        case TribeType::Hoodrick: return 7;
        case TribeType::Luxidoor: return 2;
        case TribeType::Quetzali: return 7;
        case TribeType::Yadakk: return 7;
        default:                  return 5;
    }
}

TechId Tribe::defaultStartTech(TribeType type) {
    // Wiki: each tribe except Luxidoor and Aquarion has a unique starting technology.
    // (Tech names here are labels; resolve to Tech* via Tech::findByName.)
    switch (type) {
        case TribeType::XinXi:    return TechId::Climbing;
        case TribeType::Imperius: return TechId::Organization;
        case TribeType::Bardur:   return TechId::Hunting;
        case TribeType::Oumaji:   return TechId::Riding;
        case TribeType::Kickoo:   return TechId::Fishing;
        case TribeType::Hoodrick: return TechId::Archery;
        case TribeType::Luxidoor: return TechId::Count; // nullptr equivalent
        case TribeType::Vengir:   return TechId::Smithery;
        case TribeType::Zebasi:   return TechId::Farming;
        case TribeType::AiMo:     return TechId::Philosophy;
        case TribeType::Quetzali: return TechId::Strategy;
        case TribeType::Yadakk:   return TechId::Roads;
        // case TribeType::Aquarion: return TechId::Count;
        // case TribeType::Elyrion:  return TechId::ForestMagic;
        // case TribeType::Polaris:  return TechId::Frostwork;
        // case TribeType::Cymanti:  return TechId::Farming;
        default:                  return TechId::Count;
    }
}

UnitType Tribe::defaultStartUnitType(TribeType type) {
    switch (type) {
        // Regular tribes
        case TribeType::XinXi:    return UnitType::Warrior;
        case TribeType::Imperius: return UnitType::Warrior;
        case TribeType::Bardur:   return UnitType::Warrior;
        case TribeType::Oumaji:   return UnitType::Rider;
        case TribeType::Kickoo:   return UnitType::Warrior;
        case TribeType::Hoodrick: return UnitType::Archer;
        case TribeType::Luxidoor: return UnitType::Warrior;
        case TribeType::Vengir:   return UnitType::Swordsman;
        case TribeType::Zebasi:   return UnitType::Warrior;
        case TribeType::AiMo:     return UnitType::MindBender;
        case TribeType::Quetzali: return UnitType::Defender;
        case TribeType::Yadakk:   return UnitType::Warrior;

        // Special tribes
        case TribeType::Aquarion: return UnitType::Mermaid;
        case TribeType::Elyrion:  return UnitType::Warrior;
        case TribeType::Polaris:  return UnitType::Mooni;
        case TribeType::Cymanti:  return UnitType::Shaman;
        default:                  return UnitType::Unknown;
    }
}

Tribe Tribe::makeDefault(TribeType type) {
    const char* nm = Tribe::defaultName(type);
    const uint8_t stars = Tribe::defaultStartStars(type);

    TechId techId = Tribe::defaultStartTech(type);
    UnitType unitType = Tribe::defaultStartUnitType(type);

    return Tribe(type, std::string(nm), stars, techId, unitType);
}

const Unit& Tribe::getStartUnit() const {
    return startUnit;
}