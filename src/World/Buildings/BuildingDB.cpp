#include "Buildings/BuildingDB.h"

namespace BuildingDB {

// NIE constexpr — bo C++/clang często wywala na inicjalizacji lambdą w constexpr.
// I tak jest super szybkie i robi się raz.
static const std::array<BuildingInfo, 256> kInfo = []{
    std::array<BuildingInfo, 256> a{}; // wszystko domyślnie = zero

    auto set = [&](BuildingTypeEnum bt, BuildingInfo bi) {
        a[static_cast<uint8_t>(bt)] = bi;
    };

    const TerrainMask kMonumentTerrain = TerrainMask(
    terrainBit(BaseTerrainEnum::Land) | terrainBit(BaseTerrainEnum::Water)
);

    // ===== Resource Buildings (dopasuj koszty/tech wg Twojej bazy) =====
    set(BuildingTypeEnum::Farm, {
        /*cost*/5,
        /*tech*/TechId::Farming,
        /*terrain*/TerrainMask(terrainBit(BaseTerrainEnum::Land)),
        /*res*/ResourceMask(resourceBit(ResourcesEnum::Crops)),
        /*pop*/2,
        /*territory*/true,
        /*monument*/false
    });

    set(BuildingTypeEnum::Mine, {
        5, TechId::Mining,
        TerrainMask(terrainBit(BaseTerrainEnum::Mountain)),
        ResourceMask(resourceBit(ResourcesEnum::Metal)),
        2,
        true, false
    });

    set(BuildingTypeEnum::LumberHut, {
        2, TechId::Forestry,
        TerrainMask(terrainBit(BaseTerrainEnum::Land)),
        ResourceMask(resourceBit(ResourcesEnum::Forest)),
        1,
        true, false
    });

    set(BuildingTypeEnum::Port, {
        7, TechId::Fishing,
        TerrainMask(terrainBit(BaseTerrainEnum::Water)),
        0,
        1,
        true, false
    });


    set(BuildingTypeEnum::Forge, {
        5, TechId::Smithery,
        TerrainMask(terrainBit(BaseTerrainEnum::Land)),
        ResourceMask(resourceBit(ResourcesEnum::None)),
        0,
        true, false
    });

    set(BuildingTypeEnum::Sawmill, {
        5, TechId::Mathematics,
        TerrainMask(terrainBit(BaseTerrainEnum::Land)),
        ResourceMask(resourceBit(ResourcesEnum::None)),
        0,
        true, false
    });

    set(BuildingTypeEnum::Windmill, {
        5, TechId::Construction,
        TerrainMask(terrainBit(BaseTerrainEnum::Land)),
        ResourceMask(resourceBit(ResourcesEnum::None)),
        0,
        true, false
    });

    set(BuildingTypeEnum::Market, {
        5, TechId::Trade,
        TerrainMask(terrainBit(BaseTerrainEnum::Land)),
        ResourceMask(resourceBit(ResourcesEnum::None)),
        0,
        true, false
    });

    // ===== Monuments (tu możesz dopisać własne reguły, często nie są “buildable” normalnie) =====
    set(BuildingTypeEnum::AltarOfPeace,  {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    set(BuildingTypeEnum::EmperorsTomb,  {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    set(BuildingTypeEnum::EyeOfGod,      {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    set(BuildingTypeEnum::GateOfPower,   {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    set(BuildingTypeEnum::GrandBazaar,   {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    set(BuildingTypeEnum::ParkOfFortune, {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    set(BuildingTypeEnum::TowerOfWisdom, {0, TechId::Count, kMonumentTerrain, 0, 3, true, true});
    return a;
}();

const BuildingInfo& info(BuildingTypeEnum t) {
    return kInfo[static_cast<uint8_t>(t)];
}

} // namespace BuildingDB