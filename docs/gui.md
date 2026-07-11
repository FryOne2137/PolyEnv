# GUI

The SFML GUI is a debugging and replay-inspection tool. It uses the same game
engine and replay format as Python.

## Build And Run

```bash
git clone https://github.com/FryOne2137/PolyEnv.git
cd PolyEnv

cmake -S . -B build \
  -DGAME_ENGINE_BUILD_GUI=ON \
  -DGAME_ENGINE_BUILD_APPS=OFF \
  -DGAME_ENGINE_BUILD_PYTHON_BINDINGS=OFF

cmake --build build --target game_engine_gui -j
./build/game_engine_gui
```

Run the executable from the repository root so it can find `assets/` and
`data/Units.json`. Building requires CMake 3.20+ and a C++20-capable compiler.

## Save And View Replays

In a normal game, select `Save .polygame` from the File menu. Select
`Load .polygame` to open a replay viewer.

The viewer is read-only and provides a timeline, `Next move`, automatic replay,
and an adjustable playback interval. On macOS, load and save use the system
file picker.

## Game Rules In The GUI

The GUI shows legal actions from the engine. It blocks other actions while a
city reward is pending. A city or village can be captured only by an own unit
standing on it that has not moved or attacked this turn.
