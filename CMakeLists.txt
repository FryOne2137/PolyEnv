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
        src/world/Map.cpp
        src/world/Map.h
        src/content/tribes/Tribe.cpp
        src/content/tribes/Tribe.h
        src/player/Player.cpp
        src/player/Player.h
        src/world/Pos.cpp
        src/world/Pos.h
        src/content/skills/UnitSkill.cpp
        src/content/skills/UnitSkill.h
        src/content/skills/UnitSkillTools.cpp
        src/content/skills/UnitSkillTools.h
        src/world/Tile.cpp
        src/world/Tile.h
        src/world/terrain/BaseTerrainEnum.cpp
        src/world/terrain/BaseTerrainEnum.h
        src/world/terrain/ResourcesEnum.cpp
        src/world/terrain/ResourcesEnum.h
        src/content/settlements/City.cpp
        src/content/settlements/City.h
        src/content/settlements/Settlement.cpp
        src/content/settlements/Settlement.h
        src/world/terrain/SettlementTypeEnum.cpp
        src/world/terrain/SettlementTypeEnum.h
        src/world/terrain/SettlementId.cpp
        src/world/terrain/SettlementId.h
        src/world/terrain/BuildingTypeEnum.cpp
        src/world/terrain/BuildingTypeEnum.h
        src/world/terrain/RoadBridgeEnum.cpp
        src/world/terrain/RoadBridgeEnum.h
        src/world/terrain/VisibilityEnum.cpp
        src/world/terrain/VisibilityEnum.h
        src/world/MapGenerator.cpp
        src/world/MapGenerator.h
        src/game/Game.cpp
        src/game/Game.h
        src/content/units/UnitFactory.cpp
        src/content/units/UnitFactory.h
        src/systems/TurnSystem.cpp
        src/systems/TurnSystem.h
        src/systems/MovementSystem.cpp
        src/systems/MovementSystem.h
        src/systems/CombatSystem.cpp
        src/systems/CombatSystem.h
        src/systems/InteractionSystem.cpp
        src/systems/InteractionSystem.h
        src/systems/VisionSystem.cpp
        src/systems/VisionSystem.h
        src/core/Ids.cpp
        src/core/Ids.h
        src/content/tech/TechDB.cpp
        src/content/tech/TechDB.h
        src/systems/TechSystem.cpp
        src/systems/TechSystem.h
        src/systems/BuildingSystem.cpp
        src/systems/BuildingSystem.h
        src/content/buildings/BuildingDB.cpp
        src/content/buildings/BuildingDB.h
        src/systems/TileActionSystem.cpp
        src/systems/TileActionSystem.h
        src/systems/UnitUpgradeSystem.cpp
        src/systems/UnitUpgradeSystem.h
        src/systems/UnitSpawnSystem.cpp
        src/systems/UnitSpawnSystem.h
        src/systems/CitiesConnectionSystem.cpp
        src/systems/CitiesConnectionSystem.h
        src/systems/MonumentSystem.cpp
        src/systems/MonumentSystem.h
        src/systems/LighthouseSystem.cpp
        src/systems/LighthouseSystem.h
        src/systems/ScoreSystem.cpp
        src/systems/ScoreSystem.h
        src/systems/CitySystem.cpp
        src/systems/CitySystem.h
        src/systems/UnitSystem.cpp
        src/systems/UnitSystem.h
        src/systems/PlayerSystem.cpp
        src/systems/PlayerSystem.h
        src/systems/InfiltrationSystem.cpp
        src/systems/InfiltrationSystem.h
        src/systems/StarsSystem.cpp
        src/systems/StarsSystem.h
        src/systems/CityRewardSystem.cpp
        src/systems/CityRewardSystem.h
        src/systems/GameDataSystem.cpp
        src/systems/GameDataSystem.h
        src/ai/Action.cpp
        src/ai/Action.h
        src/ai/ActionSpace.cpp
        src/ai/ActionSpace.h
        src/ai/IGameState.cpp
        src/ai/IGameState.h
        src/ai/GameStateAdapter.cpp
        src/ai/GameStateAdapter.h
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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/content/buildings
        ${CMAKE_CURRENT_SOURCE_DIR}/src/content/settlements
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
