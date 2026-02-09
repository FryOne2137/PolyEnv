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


struct Action {
    enum class Type { Move, Attack, Heal, EndTurn, BuyTech, UpgradeCity, Build, TileAction, UnitUpgrade };
    Type type;
    PlayerId pid;
    UnitId unit = kNoUnit;
    CityId city = kNoCity;
    Pos pos = {0,0};
    Pos target = {0,0};
    TechId tech = TechId::Count;
    BuildingTypeEnum building = BuildingTypeEnum::None;
    CityUpgradeChoice upgrade = CityUpgradeChoice::None;
    // minimalny payload; rozbudujesz w razie potrzeby
};


#endif //GAME_ENGINE_ACTION_H