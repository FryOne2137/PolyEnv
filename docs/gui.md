# GUI

The SFML GUI is a debug and replay-inspection tool for the engine. It is not a
separate rules implementation: normal map actions are translated to engine
`action_id` values and recorded by the same C++ replay recorder as Python.

## Build And Run

Build the GUI from a source checkout. You need a C++20-capable compiler and
CMake `>=3.20`. CMake downloads SFML automatically on the first configure.

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

Run the executable from the repository root so the GUI can find `assets/` and
`data/Units.json`. ZeroMQ is optional; install it only when using the GUI bot
client. On macOS:

```bash
brew install zeromq
```

## Normal Game

Create a game from the tribe selection screen. The map controls configure map
size, seed, initial land, smoothing, and relief. The right panel shows player
state, technology options, and city reward choices.

Use `File` in the upper-right corner to save the current game as `.polygame`.
On macOS this opens the system save dialog.

## Replay Viewer

Choose `File` → `Load .polygame` to enter a read-only replay viewer. On macOS
the GUI opens the system file dialog.

The viewer provides:

- a timeline slider to seek to any recorded move,
- `Next move` to advance one action,
- `Auto replay` to advance automatically,
- an editable interval in seconds, initially `0.20`.

The viewer reconstructs each move once when loading and keeps the resulting
states in memory. Seeking through the slider therefore does not re-simulate
the complete game on every change.

## City Rewards And Capture

If a city reaches a level that requires a reward, the engine blocks other
actions until a reward is selected. The GUI reward buttons resolve the pending
city reward even when another tile is selected.

Capturing a village or city requires an own unit to stand on that tile without
having moved or attacked during the current turn.
