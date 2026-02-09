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

# ---- nlohmann/json (header-only) ----
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(ENGINE_SOURCES
        src/content/units/Unit.cpp
        src/content/units/Unit.h
        src/World/map.cpp
        src/World/map.h
        src/content/tribes/Tribe.cpp
        src/content/tribes/Tribe.h
        src/Player/Player.cpp
        src/Player/Player.h
        src/World/Pos.cpp
        src/World/Pos.h
        src/content/skills/UnitSkill.cpp
        src/content/skills/UnitSkill.h
        src/content/skills/UnitSkillTools.cpp
        src/content/skills/UnitSkillTools.h
        src/World/tile.cpp
        src/World/tile.h
        src/World/terrain/baseTerrainEnum.cpp
        src/World/terrain/baseTerrainEnum.h
        src/World/terrain/resourcesEnum.cpp
        src/World/terrain/resourcesEnum.h
        src/World/Settlements/City.cpp
        src/World/Settlements/City.h
        src/World/Settlements/Settlement.cpp
        src/World/Settlements/Settlement.h
        src/World/terrain/SettlementTypeEnum.cpp
        src/World/terrain/SettlementTypeEnum.h
        src/World/terrain/SettlementId.cpp
        src/World/terrain/SettlementId.h
        src/World/terrain/BuildingTypeEnum.cpp
        src/World/terrain/BuildingTypeEnum.h
        src/World/terrain/RoadBridgeEnum.cpp
        src/World/terrain/RoadBridgeEnum.h
        src/World/terrain/VisibilityEnum.cpp
        src/World/terrain/VisibilityEnum.h
        src/World/MapGenerator.cpp
        src/World/MapGenerator.h
        src/game/Game.cpp
        src/game/Game.h
        src/content/units/UnitFactory.cpp
        src/content/units/UnitFactory.h
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
        src/content/tech/TechDB.cpp
        src/content/tech/TechDB.h
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
        src/Systems/CityRewardSystem.cpp
        src/Systems/CityRewardSystem.h
        src/Systems/GameDataSystem.cpp
        src/Systems/GameDataSystem.h
        src/ai/Action.cpp
        src/ai/Action.h
        src/ai/IGameState.cpp
        src/ai/IGameState.h
)

# ---- Engine library (no mains, no SFML) ----
add_library(game_engine_lib STATIC
        ${ENGINE_SOURCES}
)

# nlohmann/json provides the imported target nlohmann_json::nlohmann_json
target_link_libraries(game_engine_lib PUBLIC nlohmann_json::nlohmann_json)

# Ensure the standard is applied even if the IDE/toolchain overrides defaults
target_compile_features(game_engine_lib PUBLIC cxx_std_20)

target_include_directories(game_engine_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/skills
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/units

        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/world
        ${CMAKE_CURRENT_SOURCE_DIR}/src/world/Buildings
        ${CMAKE_CURRENT_SOURCE_DIR}/src/world/Settlements
)

# ---- Console/core executable (uses main.cpp) ----
add_executable(game_engine_core
        apps/cli/main.cpp
)
target_link_libraries(game_engine_core PRIVATE game_engine_lib)
target_compile_features(game_engine_core PRIVATE cxx_std_20)

# ---- GUI executable (uses main_gui.cpp + SFML) ----
add_executable(game_engine_gui
        apps/gui/main_gui.cpp
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
