//
// Created by Fryderyk Niedzwiecki on 11/01/2026.
//

#ifndef GAME_ENGINE_TILE_H
#define GAME_ENGINE_TILE_H
#include "terrain/BaseTerrainEnum.h"
#include "terrain/BuildingTypeEnum.h"
#include "terrain/ResourcesEnum.h"
#include "terrain/RoadBridgeEnum.h"
#include "terrain/SettlementId.h"
#include "terrain/SettlementTypeEnum.h"
#include "terrain/VisibilityEnum.h"
#include "../content/tribes/Tribe.h"
#include "core/Ids.h"


class Tile {
public:

    // Read
    ResourcesEnum getResource() const { return resource; }
    BaseTerrainEnum getBaseTerrain() const { return baseTerrain; }
    SettlementTypeEnum getSettlementType() const { return settlementType; }
    SettlementId getSettlementId() const { return settlementId; }
    BuildingTypeEnum getBuildingType() const { return buildingType; }
    RoadBridgeEnum getRoadBridge() const { return roadBridge; }
    VisibilityEnum getVisibility() const { return visibility; }
    TribeType getTribe() const { return tribe; }

    // Territory / city control (not the settlement that sits on the tile)
    CityId getTerritoryCityId() const { return territoryCityId; }

    // Write
    void setResource(ResourcesEnum v) { resource = v; }

    // Resource flag helpers
    bool hasResourceFlag(ResourcesEnum flag) const ;

    void addResourceFlag(ResourcesEnum flag);

    void removeResourceFlag(ResourcesEnum flag);
    void setBaseTerrain(BaseTerrainEnum v) { baseTerrain = v; }
    void setSettlement(SettlementTypeEnum t, SettlementId id) { settlementType = t; settlementId = id; }
    void clearSettlement() { settlementType = SettlementTypeEnum::None; settlementId = kNoSettlement; }
    void setBuildingType(BuildingTypeEnum v) { buildingType = v; }
    void setRoadBridge(RoadBridgeEnum v) { roadBridge = v; }
    void setVisibility(VisibilityEnum v) { visibility = v; }
    void setTribe(TribeType v) { tribe = v; }
    void setTerritoryCityId(CityId v) { territoryCityId = v; }
    void clearTerritory() { territoryCityId = kNoCity; }


private:
    //visibility of difrent players
    VisibilityEnum visibility = VisibilityEnum::None;
    CityId territoryCityId = kNoCity;
    RoadBridgeEnum roadBridge = RoadBridgeEnum::None;
    BuildingTypeEnum buildingType = BuildingTypeEnum::None;
    SettlementTypeEnum settlementType = SettlementTypeEnum::None;
    SettlementId   settlementId = kNoSettlement;
    ResourcesEnum resource = ResourcesEnum::None;
    BaseTerrainEnum baseTerrain = BaseTerrainEnum::Ocean;
    TribeType tribe = TribeType::Unknown;


};


#endif //GAME_ENGINE_TILE_H