//
// Created by Fryderyk Niedzwiecki on 14/01/2026.
//

#include "UnitFactory.h"
#include "Unit.h"
#include "tech/TechDB.h" // TechId
#include <algorithm>


Unit UnitFactory::create(UnitType type, PlayerId ownerId, Pos pos) {
    Unit u;
    u.setType(type);
    u.setOwnerId(ownerId);
    u.setPos(pos);

    applyBaseStats(u);

    u.setMovedThisTurn(false);
    u.setAttackedThisTurn(false);

    return u;
}

int UnitFactory::getUnitCost(UnitType type) {
    Unit u;
    u.setType(type);
    applyBaseStats(u);
    return std::max(0, u.getCost());
}


void UnitFactory::applyBaseStats(Unit& u) {
    // Defaults (safe fallback)
    u.setMaxHealth(10);
    u.setHealth(10);
    u.setAttack(1.0f);
    u.setDefense(1.0f);
    u.setMovePoints(1);
    u.setRange(1);
    u.setCost(0);
    // By default units can become veterans unless excluded by rules below.

    // Vision rule: most units have 1; only units with Scout skill have 2.
    u.setVisionRange(1);

    // By default: no tech required to spawn/train this unit.
    u.setRequiredTechToSpawn(TechId::Count);

    switch (u.getType()) {
        // -----------------------
        // Land
        // -----------------------
        case UnitType::Warrior:
            u.setCost(2); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(2.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Fortify);
            break;

        case UnitType::Archer:
            u.setRequiredTechToSpawn(TechId::Archery);
            u.setCost(3); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(2);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Fortify);
            break;

        case UnitType::Defender:
            u.setRequiredTechToSpawn(TechId::Strategy);
            u.setCost(3); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(1.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Fortify);
            break;

        case UnitType::Rider:
            u.setRequiredTechToSpawn(TechId::Riding);
            u.setCost(3); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::Fortify);
            break;

        case UnitType::Cloak:
            u.setRequiredTechToSpawn(TechId::Diplomacy);
            u.setCost(8); u.setMaxHealth(5); u.setHealth(5);
            u.setAttack(0.0f); u.setDefense(0.5f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Hide);
            u.addSkill(UnitSkill::Creep);
            u.addSkill(UnitSkill::Infiltrate);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::Scout);
            u.addSkill(UnitSkill::StaticSkill);
            break;


        case UnitType::MindBender:
            u.setRequiredTechToSpawn(TechId::Philosophy);
            u.setCost(5); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(0.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Heal);
            u.addSkill(UnitSkill::Convert);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::Swordsman:
            u.setRequiredTechToSpawn(TechId::Smithery);
            u.setCost(5); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(3.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            break;

        case UnitType::Catapult:
            u.setRequiredTechToSpawn(TechId::Mathematics);
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(4.0f); u.setDefense(0.0f);
            u.setMovePoints(1); u.setRange(3);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::Knight:
            u.setRequiredTechToSpawn(TechId::Chivalry);
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(3.5f); u.setDefense(1.0f);
            u.setMovePoints(3); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Persist);
            u.addSkill(UnitSkill::Fortify);
            break;

        case UnitType::Giant:
            // N/A cost on wiki (super unit)
            u.setCost(0); u.setMaxHealth(40); u.setHealth(40);
            u.setAttack(5.0f); u.setDefense(4.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Bunny:
        case UnitType::Bunta:
            // N/A cost on wiki
            u.setCost(0); u.setMaxHealth(20); u.setHealth(20);
            u.setAttack(5.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Independent);
            break;

        // -----------------------
        // Naval
        // -----------------------
        case UnitType::Raft:
            u.setRequiredTechToSpawn(TechId::Fishing);
            // Health varies (carried unit)
            u.setCost(0); u.setMaxHealth(0); u.setHealth(0);
            u.setAttack(0.0f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::StaticSkill);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::Scout:
            u.setRequiredTechToSpawn(TechId::Sailing);
            // This is "Scout" in current Polytopia naval table
            // Health varies (carried unit)
            u.setCost(5); u.setMaxHealth(0); u.setHealth(0);
            u.setAttack(2.0f); u.setDefense(1.0f);
            u.setMovePoints(3); u.setRange(2);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::Scout);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Rammer:
            u.setRequiredTechToSpawn(TechId::Ramming);
            // Health varies (carried unit)
            u.setCost(5); u.setMaxHealth(0); u.setHealth(0);
            u.setAttack(3.0f); u.setDefense(3.0f);
            u.setMovePoints(3); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Bomber:
            u.setRequiredTechToSpawn(TechId::Navigation);
            // Health varies (carried unit)
            u.setCost(15); u.setMaxHealth(0); u.setHealth(0);
            u.setAttack(3.0f); u.setDefense(2.0f);
            u.setMovePoints(2); u.setRange(3);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::Splash);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Dinghy:
            // N/A cost on wiki
            u.setCost(0); u.setMaxHealth(5); u.setHealth(5);
            u.setAttack(0.0f); u.setDefense(0.5f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::Hide);
            u.addSkill(UnitSkill::Creep);
            u.addSkill(UnitSkill::Infiltrate);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::Pirate:
            // N/A cost on wiki
            u.setCost(0); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(2.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Surprise);
            u.addSkill(UnitSkill::Independent);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Juggernaut:
            // N/A cost on wiki
            u.setCost(0); u.setMaxHealth(40); u.setHealth(40);
            u.setAttack(4.0f); u.setDefense(4.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Carry);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::Stomp);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        // -----------------------
        // Aquarion
        // -----------------------
        case UnitType::Mermaid:
            u.setCost(2); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(2.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Fortify);
            u.addSkill(UnitSkill::Amphibious);
            break;

        case UnitType::AquaticAmphibian:
            u.setRequiredTechToSpawn(TechId::Riding);
            u.setCost(3); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Amphibious);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::Fortify);
            break;

        case UnitType::MermaidArcher:
            u.setRequiredTechToSpawn(TechId::Archery);
            u.setCost(3); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(2);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Fortify);
            u.addSkill(UnitSkill::Amphibious);
            break;

        case UnitType::MermaidDefender:
            u.setRequiredTechToSpawn(TechId::Strategy);
            u.setCost(3); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(1.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Fortify);
            u.addSkill(UnitSkill::Amphibious);
            break;

        case UnitType::Swordsmaid:
            u.setCost(5); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(3.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Amphibious);
            break;

        case UnitType::Scuba:
            u.setRequiredTechToSpawn(TechId::Diplomacy);
            u.setCost(8); u.setMaxHealth(5); u.setHealth(5);
            u.setAttack(0.0f); u.setDefense(0.5f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Hide);
            u.addSkill(UnitSkill::Creep);
            u.addSkill(UnitSkill::Infiltrate);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Scout);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::StaticSkill);
            u.addSkill(UnitSkill::Amphibious);
            break;

        case UnitType::Siren:
            u.setRequiredTechToSpawn(TechId::Philosophy);
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(0.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Heal);
            u.addSkill(UnitSkill::Convert);
            u.addSkill(UnitSkill::Amphibious);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::Shark:
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(3.5f); u.setDefense(2.0f);
            u.setMovePoints(3); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Surprise);
            u.addSkill(UnitSkill::WaterOnly);
            break;

        case UnitType::YellyBelly:
            u.setRequiredTechToSpawn(TechId::Navigation);
            u.setCost(8); u.setMaxHealth(20); u.setHealth(20);
            u.setAttack(0.0f); u.setDefense(2.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Tentacles);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Puffer:
            u.setRequiredTechToSpawn(TechId::Mathematics);
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(4.0f); u.setDefense(0.0f);
            u.setMovePoints(2); u.setRange(3);
            u.addSkill(UnitSkill::Drench);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::TridentionAq:
            u.setRequiredTechToSpawn(TechId::Chivalry);
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.5f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(2);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Persist);
            u.addSkill(UnitSkill::Amphibious);
            break;

        case UnitType::CrabAq:
            // N/A cost on wiki (super unit)
            u.setCost(0); u.setMaxHealth(40); u.setHealth(40);
            u.setAttack(4.0f); u.setDefense(5.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Amphibious);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::AutoFlood);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        // -----------------------
        // Elyrion (∑∫ỹriȱŋ)
        // -----------------------
        case UnitType::Polytaur:
            u.setCost(3); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(3.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Fortify);
            u.addSkill(UnitSkill::Independent);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::DragonEgg:
            // N/A cost on wiki (super unit family); Range is N/A there.
            u.setCost(0); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(0.0f); u.setDefense(2.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Grow);
            u.addSkill(UnitSkill::Fortify);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::BabyDragon:
            u.setCost(0); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(3.0f); u.setDefense(3.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Air);
            u.addSkill(UnitSkill::Grow);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::Scout);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::FireDragon:
            u.setCost(0); u.setMaxHealth(20); u.setHealth(20);
            u.setAttack(4.0f); u.setDefense(3.0f);
            u.setMovePoints(3); u.setRange(2);
            u.addSkill(UnitSkill::Air);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Splash);
            u.addSkill(UnitSkill::Scout);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        // -----------------------
        // Polaris
        // -----------------------
        case UnitType::IceArcher:
            u.setRequiredTechToSpawn(TechId::Archery);
            u.setCost(3); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(1.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(2);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Freeze);
            u.addSkill(UnitSkill::Fortify);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::BattleSled:
            u.setCost(5); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(3.0f); u.setDefense(2.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::Skate);
            break;

        case UnitType::Mooni:
            u.setCost(5); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(0.0f); u.setDefense(2.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Skate);
            u.addSkill(UnitSkill::AutoFreeze);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::IceFortress:
            u.setCost(15); u.setMaxHealth(20); u.setHealth(20);
            u.setAttack(4.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(2);
            u.addSkill(UnitSkill::Skate);
            u.addSkill(UnitSkill::Scout);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Gaami:
            // N/A cost on wiki (super unit)
            u.setCost(0); u.setMaxHealth(30); u.setHealth(30);
            u.setAttack(4.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::AutoFreeze);
            u.addSkill(UnitSkill::FreezeArea);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        // -----------------------
        // Cymanti
        // -----------------------
        case UnitType::Hexapod:
            u.setRequiredTechToSpawn(TechId::Riding);
            u.setCost(3); u.setMaxHealth(5); u.setHealth(5);
            u.setAttack(3.0f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Escape);
            u.addSkill(UnitSkill::Creep);
            u.addSkill(UnitSkill::Sneak);
            break;

        case UnitType::Kiton:
            u.setRequiredTechToSpawn(TechId::Strategy);
            u.setCost(3); u.setMaxHealth(15); u.setHealth(15);
            u.setAttack(1.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Poison);
            u.addSkill(UnitSkill::Creep);
            break;

        case UnitType::Phychi:
            u.setRequiredTechToSpawn(TechId::Archery);
            u.setCost(3); u.setMaxHealth(5); u.setHealth(5);
            u.setAttack(0.7f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(2);
            u.addSkill(UnitSkill::Air);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Poison);
            u.addSkill(UnitSkill::Surprise);
            u.addSkill(UnitSkill::DoubleAttack);
            break;

        case UnitType::Shaman:
            u.setRequiredTechToSpawn(TechId::Philosophy);
            u.setCost(5); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(1.0f); u.setDefense(1.0f);
            u.setMovePoints(1); u.setRange(1);
            // Wiki uses "Parasite"; your enum has no Parasite, so we map it to Convert.
            u.addSkill(UnitSkill::Convert);
            u.addSkill(UnitSkill::Swarm);
            break;

        case UnitType::Raychi:
            u.setRequiredTechToSpawn(TechId::Sailing);
            u.setCost(5); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(3.0f); u.setDefense(2.0f);
            u.setMovePoints(3); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Dash);
            break;

        case UnitType::Exida:
            u.setRequiredTechToSpawn(TechId::Mathematics);
            u.setCost(8); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(3.0f); u.setDefense(1.0f);
            u.setMovePoints(2); u.setRange(3);
            u.addSkill(UnitSkill::Poison);
            u.addSkill(UnitSkill::Splash);
            u.addSkill(UnitSkill::Creep);
            break;

        case UnitType::Doomux:
            u.setRequiredTechToSpawn(TechId::Chivalry);
            u.setCost(10); u.setMaxHealth(20); u.setHealth(20);
            u.setAttack(3.5f); u.setDefense(2.0f);
            u.setMovePoints(3); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Creep);
            u.addSkill(UnitSkill::Explode);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::MothC:
            u.setRequiredTechToSpawn(TechId::Diplomacy);
            u.setCost(5); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(0.0f); u.setDefense(0.1f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::Air);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Sneak);
            u.addSkill(UnitSkill::Infiltrate);
            u.addSkill(UnitSkill::Poison);
            u.addSkill(UnitSkill::StaticSkill);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::LarvaC:
            // N/A cost on wiki (spawned)
            u.setCost(0); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(2.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Creep);
            u.addSkill(UnitSkill::Surprise);
            u.addSkill(UnitSkill::Independent);
            u.addSkill(UnitSkill::StaticSkill);
            u.addSkill(UnitSkill::Grow);
            break;

        case UnitType::InsectEgg:
            // N/A cost on wiki (spawned)
            u.setCost(0); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(2.0f); u.setDefense(3.0f);
            u.setMovePoints(0); u.setRange(1);
            u.addSkill(UnitSkill::Grow);
            u.addSkill(UnitSkill::Stiff);
            u.addSkill(UnitSkill::Explode);
            u.addSkill(UnitSkill::StaticSkill);
            break;

        case UnitType::Boomchi:
            u.setCost(5); u.setMaxHealth(10); u.setHealth(10);
            u.setAttack(3.0f); u.setDefense(3.0f);
            u.setMovePoints(1); u.setRange(1);
            u.addSkill(UnitSkill::Dash);
            u.addSkill(UnitSkill::Explode);
            u.addSkill(UnitSkill::Stiff);
            break;

        case UnitType::LivingIsland:
            u.setRequiredTechToSpawn(TechId::Navigation);
            u.setCost(20); u.setMaxHealth(20); u.setHealth(20);
            u.setAttack(4.0f); u.setDefense(4.0f);
            u.setMovePoints(2); u.setRange(1);
            u.addSkill(UnitSkill::WaterOnly);
            u.addSkill(UnitSkill::Stomp);
            u.addSkill(UnitSkill::Algae);
            u.addSkill(UnitSkill::StaticSkill);
            u.addSkill(UnitSkill::Poison);
            break;


        // -----------------------
        // Removed / legacy units (optional)
        // -----------------------

        default:
            break;
}
    // Veteran rule (Polytopia): after 3 kills a unit can be promoted, except for:
    // - Naval units
    // - Super units
    // - Dagger & Pirate
    // - Enchanted units (Polytaur, Navalon)
    // - Units not controlled by any player (Bunny, Bunta)
    // Additionally, units with StaticSkill cannot become veterans.
    bool canVeteran = true;
    switch (u.getType()) {
        // Naval
        case UnitType::Raft:
        case UnitType::Rammer:
        case UnitType::Scout:
        case UnitType::Bomber:
        case UnitType::Juggernaut:
        case UnitType::Dinghy:
        case UnitType::Pirate:
            canVeteran = false;
            break;

        // Super / special non-promotable
        case UnitType::Giant:
        case UnitType::CrabAq:
        case UnitType::DragonEgg:
        case UnitType::BabyDragon:
        case UnitType::FireDragon:
        case UnitType::Gaami:
        case UnitType::LivingIsland:
        case UnitType::Polytaur:
        case UnitType::Bunny:
        case UnitType::Bunta:
            canVeteran = false;
            break;

        default:
            break;
    }

    if (u.hasSkill(UnitSkill::StaticSkill)) {
        canVeteran = false;
    }


    u.setVisionRange(u.hasSkill(UnitSkill::Scout) ? 2 : 1);
}