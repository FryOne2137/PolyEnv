//
// Created by Fryderyk Niedzwiecki on 25/01/2026.
//

#ifndef GAME_ENGINE_BUILDINGDB_H
#define GAME_ENGINE_BUILDINGDB_H

#pragma once
#include <array>
#include <cstdint>

#include "terrain/BuildingTypeEnum.h"
#include "terrain/BaseTerrainEnum.h"
#include "terrain/ResourcesEnum.h"
#include "tech/TechDB.h" // TechId

namespace BuildingDB {

    using TerrainMask  = uint8_t; // BaseTerrainEnum ma mało wartości -> maska bitowa OK
    using ResourceMask = uint8_t; // ResourcesEnum to już bity (1<<n)

    constexpr TerrainMask terrainBit(BaseTerrainEnum t) {
        return TerrainMask(1u) << static_cast<uint8_t>(t); // Ocean=0,Water=1,Land=2,Mountain=3
    }

    // UWAGA: ResourcesEnum to już bit flagi, więc "bit" = wartość enum
    constexpr ResourceMask resourceBit(ResourcesEnum r) {
        return static_cast<ResourceMask>(r);
    }

    struct BuildingInfo final {
        uint8_t cost = 0;                      // stars
        TechId requiredTech = TechId::Count;   // Count = brak wymogu
        TerrainMask allowedTerrainMask = 0;    // bity po BaseTerrainEnum
        ResourceMask requiredResourceMask = 0; // bity po ResourcesEnum (już gotowe)

        // How much population this building adds to the owning city.
        // Examples: Farm = 2, LumberHut = 1, Mine = 1.
        uint8_t populationGain = 0;

        bool requiresTerritory = true;         // tile musi mieć territoryCityId != kNoCity
        bool isMonument = false;
    };

    const BuildingInfo& info(BuildingTypeEnum t);

    inline int getCost(BuildingTypeEnum t) { return int(info(t).cost); }
    inline TechId getRequiredTech(BuildingTypeEnum t) { return info(t).requiredTech; }

    inline bool isDefined(BuildingTypeEnum t) {
        const auto& bi = info(t);
        return (bi.allowedTerrainMask != 0) || bi.isMonument;
    }

}

#endif //GAME_ENGINE_BUILDINGDB_H