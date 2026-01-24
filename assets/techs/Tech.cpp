//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#include "Tech.h"

#include "AquatismTech.h"
#include "ArcheryTech.h"
#include "ChivalryTech.h"
#include "ClimbingTech.h"
#include "ConstructionTech.h"
#include "DiplomacyTech.h"
#include "FarmingTech.h"
#include "FishingTech.h"
#include "ForestryTech.h"
#include "FreeSpiritTech.h"
#include "HuntingTech.h"
#include "MathematicsTech.h"
#include "MeditationTech.h"
#include "MiningTech.h"
#include "NavigationTech.h"
#include "OrganizationTech.h"
#include "PhilosophyTech.h"
#include "RammingTech.h"
#include "RidingTech.h"
#include "RoadsTech.h"
#include "SailingTech.h"
#include "SmitheryTech.h"
#include "SpiritualismTech.h"
#include "StrategyTech.h"
#include "TradeTech.h"


bool Tech::hasPrevious() const {
    return previousTech != nullptr;
}

const Tech* Tech::getPrevious() const {
    return previousTech;
}

int Tech::getTier() const {
    return tier;
}

int Tech::getPrice(int numOfCities) const {
    return numOfCities*Tech::getTier()+4;
}

std::string Tech::getName() const {
    return name;
}

Tech::Tech(int tier, const Tech* previous)
    : tier(tier), previousTech(previous) {}

const std::vector<const Tech*>& Tech::getAllTechs() {
    static const std::vector<const Tech*> all = {
        &OrganizationTech::getBase(),
        &ClimbingTech::getBase(),
        &HuntingTech::getBase(),
        &FishingTech::getBase(),

        &FarmingTech::getBase(),
        &ForestryTech::getBase(),
        &MiningTech::getBase(),
        &SailingTech::getBase(),

        &RoadsTech::getBase(),
        &TradeTech::getBase(),
        &DiplomacyTech::getBase(),

        &ArcheryTech::getBase(),
        &RidingTech::getBase(),
        &FreeSpiritTech::getBase(),
        &AquatismTech::getBase(),

        &SmitheryTech::getBase(),
        &ConstructionTech::getBase(),
        &NavigationTech::getBase(),
        &SpiritualismTech::getBase(),

        &MathematicsTech::getBase(),
        &PhilosophyTech::getBase(),
        &StrategyTech::getBase(),
        &ChivalryTech::getBase(),

        &MeditationTech::getBase(),

        &RammingTech::getBase(),
    };
    return all;
}

const Tech* Tech::findByName(const std::string& techName) {
    const auto& all = Tech::getAllTechs();
    for (const Tech* t : all) {
        if (t && t->getName() == techName) {
            return t;
        }
    }
    return nullptr;
}