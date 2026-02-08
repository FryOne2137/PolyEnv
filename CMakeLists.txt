cmake_minimum_required(VERSION 4.1)
project(game_engine)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    sfml
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.x
)

FetchContent_MakeAvailable(sfml)

set(ENGINE_SOURCES
        assets/units/Unit.cpp
        assets/units/Unit.h
        assets/skills/Skill.cpp
        assets/skills/Skill.h
        src/world/map.cpp
        src/world/map.h
        assets/tribes/Tribe.cpp
        assets/tribes/Tribe.h
        assets/techs/Tech.cpp
        assets/techs/Tech.h
        assets/techs/ClimbingTech.cpp
        assets/techs/ClimbingTech.h
        assets/techs/FishingTech.cpp
        assets/techs/FishingTech.h
        assets/techs/HuntingTech.cpp
        assets/techs/HuntingTech.h
        assets/techs/OrganizationTech.cpp
        assets/techs/OrganizationTech.h
        assets/techs/RidingTech.cpp
        assets/techs/RidingTech.h
        assets/techs/ArcheryTech.cpp
        assets/techs/ArcheryTech.h
        assets/techs/ForestryTech.cpp
        assets/techs/ForestryTech.h
        assets/techs/RoadsTech.cpp
        assets/techs/RoadsTech.h
        assets/techs/FreeSpiritTech.cpp
        assets/techs/FreeSpiritTech.h
        assets/techs/FarmingTech.cpp
        assets/techs/FarmingTech.h
        assets/techs/StrategyTech.cpp
        assets/techs/StrategyTech.h
        assets/techs/MiningTech.cpp
        assets/techs/MiningTech.h
        assets/techs/MeditationTech.cpp
        assets/techs/MeditationTech.h
        assets/techs/SailingTech.cpp
        assets/techs/SailingTech.h
        assets/techs/RammingTech.cpp
        assets/techs/RammingTech.h
        assets/techs/SpiritualismTech.cpp
        assets/techs/SpiritualismTech.h
        assets/techs/MathematicsTech.cpp
        assets/techs/MathematicsTech.h
        assets/techs/TradeTech.cpp
        assets/techs/TradeTech.h
        assets/techs/ChivalryTech.cpp
        assets/techs/ChivalryTech.h
        assets/techs/ConstructionTech.cpp
        assets/techs/ConstructionTech.h
        assets/techs/DiplomacyTech.cpp
        assets/techs/DiplomacyTech.h
        assets/techs/SmitheryTech.cpp
        assets/techs/SmitheryTech.h
        assets/techs/PhilosophyTech.cpp
        assets/techs/PhilosophyTech.h
        assets/techs/NavigationTech.cpp
        assets/techs/NavigationTech.h
        assets/techs/AquatismTech.cpp
        assets/techs/AquatismTech.h
        src/Player/Player.cpp
        src/Player/Player.h
        src/world/Pos.cpp
        src/world/Pos.h
        assets/skills/UnitSkill.cpp
        assets/skills/UnitSkill.h
        assets/skills/UnitSkillTools.cpp
        assets/skills/UnitSkillTools.h
        src/world/tile.cpp
        src/world/tile.h
        src/world/terrain/baseTerrainEnum.cpp
        src/world/terrain/baseTerrainEnum.h
        src/world/terrain/resourcesEnum.cpp
        src/world/terrain/resourcesEnum.h
        src/world/Settlements/Village.cpp
        src/world/Settlements/Village.h
        src/world/Settlements/City.cpp
        src/world/Settlements/City.h
        src/world/Settlements/upgradeRewardEnum.cpp
        src/world/Settlements/upgradeRewardEnum.h
        src/world/Settlements/Ruin.cpp
        src/world/Settlements/Ruin.h
        src/World/Settlements/Settlement.cpp
        src/World/Settlements/Settlement.h
        src/World/terrain/SettlementTypeEnum.cpp
        src/World/terrain/SettlementTypeEnum.h
        src/World/terrain/SettlementId.cpp
        src/World/terrain/SettlementId.h
        src/World/Settlements/Starfish.cpp
        src/World/Settlements/Starfish.h
        src/World/terrain/BuildingTypeEnum.cpp
        src/World/terrain/BuildingTypeEnum.h
        src/World/terrain/RoadBridgeEnum.cpp
        src/World/terrain/RoadBridgeEnum.h
        src/World/terrain/VisibilityEnum.cpp
        src/World/terrain/VisibilityEnum.h
        src/World/MapGenerator.cpp
        src/World/MapGenerator.h
        src/Game.cpp
        src/Game.h
        assets/units/UnitFactory.cpp
        assets/units/UnitFactory.h
        src/Systems/TurnSystem.cpp
        src/Systems/TurnSystem.h
        src/Systems/MovementSystem.cpp
        src/Systems/MovementSystem.h
        src/Systems/CombatSystem.cpp
        src/Systems/CombatSystem.h
        src/Systems/InteractionSystem.cpp
        src/Systems/InteractionSystem.h
        src/Systems/VisionSystem.cpp
        src/Systems/VisionSystem.h
        src/Core/Ids.cpp
        src/Core/Ids.h
        assets/tech/TechDB.cpp
        assets/tech/TechDB.h
        src/Systems/TechSystem.cpp
        src/Systems/TechSystem.h
        src/Systems/BuildingSystem.cpp
        src/Systems/BuildingSystem.h
        src/World/Buildings/BuildingDB.cpp
        src/World/Buildings/BuildingDB.h
        src/Systems/TileActionSystem.cpp
        src/Systems/TileActionSystem.h
        src/Systems/UnitUpgradeSystem.cpp
        src/Systems/UnitUpgradeSystem.h
        src/Systems/UnitSpawnSystem.cpp
        src/Systems/UnitSpawnSystem.h
        src/Systems/CitiesConnectionSystem.cpp
        src/Systems/CitiesConnectionSystem.h
        src/Systems/MonumentSystem.cpp
        src/Systems/MonumentSystem.h
        src/Systems/LighthouseSystem.cpp
        src/Systems/LighthouseSystem.h
        src/Systems/ScoreSystem.cpp
        src/Systems/ScoreSystem.h
        src/Systems/CitySystem.cpp
        src/Systems/CitySystem.h
        src/Systems/UnitSystem.cpp
        src/Systems/UnitSystem.h
        src/Systems/PlayerSystem.cpp
        src/Systems/PlayerSystem.h
        src/Systems/InfiltrationSystem.cpp
        src/Systems/InfiltrationSystem.h
        src/Systems/StarsSystem.cpp
        src/Systems/StarsSystem.h
        src/Systems/CityUpgradeSystem.cpp
        src/Systems/CityUpgradeSystem.h
)

# ---- Engine library (no mains, no SFML) ----
add_library(game_engine_lib STATIC
        ${ENGINE_SOURCES}
)

# Ensure the standard is applied even if the IDE/toolchain overrides defaults
target_compile_features(game_engine_lib PUBLIC cxx_std_20)

target_include_directories(game_engine_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/skills
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/units

        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/World
        ${CMAKE_CURRENT_SOURCE_DIR}/src/World/Buildings
        ${CMAKE_CURRENT_SOURCE_DIR}/src/World/Settlements
)

# ---- Console/core executable (uses main.cpp) ----
add_executable(game_engine_core
        main.cpp
)
target_link_libraries(game_engine_core PRIVATE game_engine_lib)
target_compile_features(game_engine_core PRIVATE cxx_std_20)

# ---- GUI executable (uses main_gui.cpp + SFML) ----
add_executable(game_engine_gui
        main_gui.cpp
        src/gui/GuiApp.cpp
        src/gui/GuiApp.h
        src/gui/TextureStore.cpp
        src/gui/TextureStore.h
        src/gui/TribeSelectScreen.cpp
        src/gui/TribeSelectScreen.h
        src/gui/MapRenderer.cpp
        src/gui/MapRenderer.h
)

target_link_libraries(game_engine_gui
        PRIVATE
        game_engine_lib
        sfml-graphics
        sfml-window
        sfml-system
)
target_compile_features(game_engine_gui PRIVATE cxx_std_20)
