# YK Engine

A proprietary C++20 2D game engine with a VS Code-style editor. You build a level in the editor by
placing prefabs and components, wiring them together and pressing **Play**; the same data runs in
a standalone player and can be exported as a desktop game.

**The engine is the product.** The demo game, *Cinder Vale*, is a test of it: a complete
two-player puzzle platformer (two characters, two hazards, gems, levers, gates, plates, a moving
platform, checkpoints, exits) built only from the engine's components and prefabs, with original
generated art, and no code of its own.

| Part | What it is | Where |
|---|---|---|
| **Engine** | Entities and components with reflection, scene/prefab/project files, input actions, animation clips and state machines, a headless fixed-step runtime on Box2D, an SDL3 renderer (layers, culling, tiling, nine-slice, parallax, particles), audio, validation, export. No game rules. | `include/`, `src/` |
| **Gameplay library** | Reusable mechanics as components: platformer controller, plates, levers, doors, moving platforms, hazards, collectibles, checkpoints, goals, level rules. | `include/yk/gameplay`, `src/gameplay` |
| **Editor** | Workbench with an Explorer, scene hierarchy, prefabs, component catalog, inspector, tabs and split views, console, problems, profiler; real level editing; Play on a copy of the scene. | `editor/` |
| **Player and tools** | `yk_player` runs any project; `yk` validates, formats, inspects and exports projects. | `player/`, `tools/yk/` |
| **Demo game** | *Cinder Vale*: a project folder (data) that consumes the engine, and the scripts that generate its art. | `YK-DemoGame/` |

## Try it

Requires CMake 3.25+, Ninja and a C++20 compiler (GCC, Clang; MSVC is untested here). The first
configure downloads hash-pinned Box2D, SDL3 and Dear ImGui. Linux also needs SDL's usual
development packages. Details, presets and the Windows and macOS notes are in
[docs/BUILDING.md](docs/BUILDING.md).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure

./build/dev/yk_editor                       # welcome screen: New Project, Open Project, the demo game
./build/dev/yk_editor YK-DemoGame           # open the demo game in the editor
./build/dev/yk_player YK-DemoGame           # play it without the editor
./build/dev/yk export YK-DemoGame --target linux --out dist --zip     # package it
```

Playing the demo: **Ember** uses **A/D** to move, **W** to jump and **S** to use a lever; **Tide**
uses the **arrow keys**, **Up** to jump and **Down** to use a lever; both must reach their exits
(each character is safe in its own pool and deadly in the other's). **R** restarts the level. In
the editor press **F5** to play, **Shift+F5** to stop.

## What the editor does

Create or open a project, open scenes in tabs, place entities and prefabs, move, resize and
rotate them with gizmos and snapping, edit properties in an Inspector generated from the
components' own declarations, wire mechanisms together (plates to doors) with pickers and
eyedroppers, import images and sounds, tune texture import settings and animation timing, edit the
input map and collision layers, validate the project, press Play on a copy of the scene (both
characters live), and export the game. The guide is [docs/EDITOR.md](docs/EDITOR.md).

## Repository layout

```text
include/yk/  src/         engine and gameplay library (Box2D and SDL stay private)
editor/core  editor/ui    editor: UI-free core library; Dear ImGui workbench, panels, scripted UI driver
player/  tools/yk/        yk_player and the yk command line
YK-DemoGame/              the demo game (data) and its art/level generators
tests/{unit,integration,editor,install}/
docs/  cmake/  scripts/   documentation, CMake modules and toolchains, verification and packaging scripts
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md): modules, systems, rules of the codebase
- [Building, testing and shipping](docs/BUILDING.md): presets, tests, packaging, export, Windows and macOS
- [Editor guide](docs/EDITOR.md): the workbench, level editing, prefabs, assets, play, export, shortcuts
- [Project format](docs/PROJECT_FORMAT.md): every file the engine reads and writes
- [Status](docs/STATUS.md): what is verified, how, and what is not done
- [Component reference](docs/components.md) (generated from the registry the Inspector uses),
  [physics API](docs/physics.md), [decisions](docs/decisions/), [the audit that started this pass](docs/AUDIT.md)
- [Dependency and font notices](THIRD_PARTY.md); the demo game's own notes are in
  [YK-DemoGame/README.md](YK-DemoGame/README.md)
