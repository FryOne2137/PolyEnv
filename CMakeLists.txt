cmake_minimum_required(VERSION 3.20)
project(game_engine VERSION 0.1.0 LANGUAGES CXX)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

if(DEFINED SKBUILD)
    set(_default_build_apps OFF)
    set(_default_build_gui OFF)
    set(_default_build_python ON)
else()
    set(_default_build_apps ON)
    set(_default_build_gui ON)
    set(_default_build_python OFF)
endif()

option(GAME_ENGINE_BUILD_APPS "Build CLI/GUI applications" ${_default_build_apps})
option(GAME_ENGINE_BUILD_GUI "Build GUI application (requires SFML)" ${_default_build_gui})
option(GAME_ENGINE_BUILD_PYTHON_BINDINGS "Build Python extension module" ${_default_build_python})
# Keep this opt-in: a PDB is useful when diagnosing crashes on Windows, but it
# can make the regular Python wheel substantially larger.
option(GAME_ENGINE_INSTALL_PDB "Install Windows PDB symbols with the Python extension" OFF)

if(GAME_ENGINE_BUILD_GUI)
    FetchContent_Declare(
        sfml
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG        2.6.x
    )
    FetchContent_MakeAvailable(sfml)

    # ---- ZeroMQ (libzmq system dependency, C API used directly via zmq.h) ----
    # Install:  brew install zeromq   /   apt install libzmq3-dev
    find_library(LIBZMQ_LIB NAMES zmq libzmq
        HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
    find_path(LIBZMQ_INCLUDE NAMES zmq.h
        HINTS /opt/homebrew/include /usr/local/include /usr/include)

    if(LIBZMQ_LIB AND LIBZMQ_INCLUDE)
        message(STATUS "Found libzmq: ${LIBZMQ_LIB}  (include: ${LIBZMQ_INCLUDE})")
        set(GAME_ENGINE_HAS_ZMQ TRUE)
    else()
        message(WARNING
            "libzmq not found — bot client will be disabled.\n"
            "Install with:  brew install zeromq   or   apt install libzmq3-dev")
        set(GAME_ENGINE_HAS_ZMQ FALSE)
    endif()
endif()

# ---- nlohmann/json (header-only) ----
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(ENGINE_SOURCES
        src/runtime/GameSession.cpp
        src/runtime/GameSession.h
        src/runtime/BeliefWorldBuilder.cpp
        src/runtime/BeliefWorldBuilder.h
        src/replay/ReplayRecorder.cpp
        src/replay/ReplayRecorder.h
        src/ai/GameStateSerializer.cpp
        src/ai/GameStateSerializer.h
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
        src/systems/DisbandSystem.cpp
        src/systems/DisbandSystem.h
        src/ai/Action.cpp
        src/ai/Action.h
        src/ai/ActionSpace.cpp
        src/ai/ActionSpace.h
        src/ai/GameStateAdapter.cpp
        src/ai/GameStateAdapter.h
)

# ---- Engine library (no mains, no SFML) ----
add_library(game_engine_lib STATIC
        ${ENGINE_SOURCES}
)
set_target_properties(game_engine_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

if(GAME_ENGINE_BUILD_APPS)
    # ---- Console/core executable (uses main.cpp) ----
    add_executable(game_engine_core
            apps/cli/main.cpp
    )
    target_link_libraries(game_engine_core PRIVATE game_engine_lib)
    target_compile_features(game_engine_core PRIVATE cxx_std_20)
endif()

if(GAME_ENGINE_BUILD_GUI)
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
            src/ai/ModelClient.cpp
            src/ai/ModelClient.h
    )

    target_link_libraries(game_engine_gui
            PRIVATE
            game_engine_lib
            sfml-graphics
            sfml-window
            sfml-system
    )

    if(GAME_ENGINE_HAS_ZMQ)
        target_include_directories(game_engine_gui PRIVATE ${LIBZMQ_INCLUDE})
        target_link_libraries(game_engine_gui PRIVATE ${LIBZMQ_LIB})
        target_compile_definitions(game_engine_gui PRIVATE GAME_ENGINE_HAS_ZMQ=1)
    else()
        target_compile_definitions(game_engine_gui PRIVATE GAME_ENGINE_HAS_ZMQ=0)
    endif()

    target_compile_features(game_engine_gui PRIVATE cxx_std_20)
endif()

if(GAME_ENGINE_BUILD_PYTHON_BINDINGS)
    # Native diagnostics for the Python extension. In a RelWithDebInfo build
    # the matching PDB lets cpptrace turn an engine frame into its C++ file and
    # line. Keeping this here means non-Python engine builds do not download it.
    # A static dependency keeps the Python wheel self-contained: importing the
    # .pyd never requires a separate cpptrace.dll beside it.
    set(CPPTRACE_BUILD_SHARED OFF CACHE BOOL "Build cpptrace as a shared library" FORCE)
    FetchContent_Declare(
        cpptrace
        GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
        GIT_TAG        v1.0.4
    )
    FetchContent_MakeAvailable(cpptrace)

    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(_game_engine MODULE
            src/python/bindings.cpp
    )
    target_link_libraries(_game_engine PRIVATE game_engine_lib)
    target_link_libraries(_game_engine PRIVATE cpptrace::cpptrace)
    target_compile_features(_game_engine PRIVATE cxx_std_20)

    # ---- Python package layout for scikit-build-core (SKBUILD) ----
    set(_GE_PY_PKG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/python/PolyEnv")

    # Install the compiled extension into the wheel's platlib under PolyEnv/
    install(TARGETS _game_engine
            LIBRARY DESTINATION "${SKBUILD_PLATLIB_DIR}/PolyEnv"
            RUNTIME DESTINATION "${SKBUILD_PLATLIB_DIR}/PolyEnv"
            ARCHIVE DESTINATION "${SKBUILD_PLATLIB_DIR}/PolyEnv")

    # A PDB must be produced by the exact same link as _game_engine.pyd.  When
    # installed next to the extension, Windows debuggers and stacktrace tools
    # can resolve native frames to C++ source locations.  It is deliberately
    # opt-in so ordinary Release wheels do not carry the extra debug artifact.
    if(MSVC AND GAME_ENGINE_INSTALL_PDB)
        install(FILES "$<TARGET_PDB_FILE:_game_engine>"
                DESTINATION "${SKBUILD_PLATLIB_DIR}/PolyEnv"
                OPTIONAL)
    endif()

    # Install the pure-Python package files and bundled data (e.g. JSON) in one go
    install(DIRECTORY "${_GE_PY_PKG_DIR}/"
            DESTINATION "${SKBUILD_PLATLIB_DIR}/PolyEnv"
            FILES_MATCHING
              PATTERN "*.py"
              PATTERN "*.json")

    # Always bundle canonical units data from repository root as package data.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/Units.json"
            DESTINATION "${SKBUILD_PLATLIB_DIR}/PolyEnv/data")
endif()
