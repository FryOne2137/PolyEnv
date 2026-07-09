//
// Created by Fryderyk Niedzwiecki on 09/02/2026.
//

#ifndef GAME_ENGINE_ACTION_H
#define GAME_ENGINE_ACTION_H
#include "Pos.h"
#include "content/tech/TechDB.h"
#include "core/Ids.h"
#include "systems/CityRewardSystem.h"
#include "terrain/BuildingTypeEnum.h"
#include "content/units/Unit.h"


struct Action {
    enum class Type { Move, Attack, Heal, EndTurn, BuyTech, UpgradeCity, Build, SpawnUnit, TileAction, UnitUpgrade };
    enum class TileActionKind {
        None = 0,
        Hunt,
        Organization,
        Fishing,
        ClearForest,
        BurnForest,
        GrowForest,
        DestroyTile,
        BuildRoad,
        BuildBridge,
        Explorer,
        FoundCity,
        Ruin,
        Starfish,
        CaptureCity
    };
    enum class UnitUpgradeKind {
        None = 0,
        RaftToScout,
        RaftToRammer,
        RaftToBomber,
        BecomeVeteran,
        Disband
    };
    Type type;
    PlayerId pid;
    UnitId unit = kNoUnit;
    CityId city = kNoCity;
    Pos pos = {0,0};
    Pos target = {0,0};
    TechId tech = TechId::Count;
    BuildingTypeEnum building = BuildingTypeEnum::None;
    UnitType spawnType = UnitType::Unknown;
    CityUpgradeChoice upgrade = CityUpgradeChoice::None;
    TileActionKind tileAction = TileActionKind::None;
    UnitUpgradeKind unitUpgrade = UnitUpgradeKind::None;
};


#endif //GAME_ENGINE_ACTION_H
