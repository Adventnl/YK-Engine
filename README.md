<p align="center"><img src="YK.png" alt="YK Engine logo" width="160"></p>

# YK Engine

## Download and install

Get the installers from [GitHub Releases](https://github.com/Adventnl/YK-Engine/releases).
On Windows x64, run the `windows-x64.exe` setup wizard. On an Apple silicon Mac, open
the `.pkg` Installer wizard or the `.dmg` and drag **YK Engine** to Applications.
The downloads include the editor, player, sample projects, and runtime libraries;
you do not need to install SDL, CMake, or a compiler. These releases are currently
unsigned, so Windows or macOS may ask you to confirm the first launch.

A proprietary C++20 2D game engine with a VS Code-style editor. You build a level in the editor by
placing prefabs and components, wiring them together and pressing **Play**; the same data runs in
a standalone player and can be exported as a desktop game.

**The engine is the product.** The demo game, *Cinder Vale*, is a test of it: a complete
two-player puzzle platformer (two characters, two hazards, gems, levers, gates, plates you can stand
on and sink, a moving platform, checkpoints, exits, two rooms that lead into each other) built only
from the engine's components and prefabs, with original generated art, and no code of its own.
*Castle Paths* in `YK-ExplorationDemo/` is a second data-only test: top-down movement, NPC
conversation, a switch and locked gate, and travel between a courtyard and hall.
`Fireboy-Watergirl-Demo/` is a separate playable level using the supplied room and pixel-art
library, with two animated characters, color-specific hazards, switches, moving platforms, and exits.

| Part | What it is | Where |
|---|---|---|
| **Engine** | Entities and components with reflection, scene/prefab/project files, input actions, animation clips and state machines, dialogue with portraits, a headless fixed-step runtime on Box2D, an SDL3 renderer (layers, Y sorting, culling, tiling, nine-slice, parallax, particles), audio, validation, export. No game rules. | `include/`, `src/` |
| **Gameplay library** | Reusable mechanics as components: platformer and top-down controllers, NPC paths, proximity interactions, gates, portals and map spawns, plus plates, levers, doors, hazards, collectibles, checkpoints, goals, level flow and event reactions. | `include/yk/gameplay`, `src/gameplay` |
| **Editor** | Workbench with an Explorer, scene hierarchy, prefabs, component catalog, inspector, a Debug view of the running game, tabs and split views, console, problems, profiler; real level editing; Play on a copy of the scene. | `editor/` |
| **Player and tools** | `yk_player` runs any project; `yk` creates, validates, formats, inspects and exports projects. | `player/`, `tools/yk/` |
| **Packaging** | `YK Engine.app` and its `.dmg` for macOS; the exporter writes a standalone `Game.app` (or Windows/Linux folder) without the editor, with icon, signing and a `.dmg` on a Mac. | `packaging/`, `scripts/`, `src/assets/Export.cpp` |
| **Demo game** | *Cinder Vale*: a project folder (data) that consumes the engine, and the scripts that generate its art. It is a sample and a test; a game can live in a repository of its own. | `YK-DemoGame/` |
| **Exploration test** | *Castle Paths*: two small top-down scenes and placeholder artwork that exercise the new reusable exploration systems. | `YK-ExplorationDemo/` |
| **Fireboy and Watergirl sample** | Single-screen co-op level assembled from the supplied room, pixel-art cutouts and character poses. | `Fireboy-Watergirl-Demo/` |

## Use it (macOS)

Open the `YKEngine-<version>-macos-<arch>.dmg` (built by `scripts/package-macos.sh`, and kept as the
`macos-engine` artifact of every CI run), drag **YK Engine** to Applications and open it. The
welcome screen offers New Project, Open Project, recent projects and the three sample games. **Build >
Export Game** writes a standalone `GameName.app`, optionally signed and in a `.dmg`. No terminal is
involved; logs and crash reports are in `~/Library/Logs/`. Until the app is notarized with a
Developer ID (needs an Apple account; see [docs/BUILDING.md](docs/BUILDING.md#macos)), another Mac
asks for Control-click > Open once.

## Build it

Requires CMake 3.25+, Ninja and a C++20 compiler (GCC, Clang, MSVC; all three are built and tested in CI). The first
configure downloads hash-pinned Box2D, SDL3 and Dear ImGui. Linux also needs SDL's usual
development packages. Details, presets and the Windows and macOS notes are in
[docs/BUILDING.md](docs/BUILDING.md).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure

./build/dev/yk_editor                       # welcome screen: projects and both demos
./build/dev/yk_editor YK-DemoGame           # open the demo game in the editor
./build/dev/yk_player YK-DemoGame           # play it without the editor
./build/dev/yk_player Fireboy-Watergirl-Demo  # play the new reference-art level
./build/dev/yk_player YK-ExplorationDemo     # play the top-down exploration test
./build/dev/yk export YK-DemoGame --target linux --out dist --zip     # package it
./build/dev/yk new ~/Games/MyGame --name "My Game"                    # a new project anywhere
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
packaging/                macOS bundle template, entitlements, icons
tests/                    unit, integration, editor UI scripts, install, macOS bundle, diagnostics, external project
docs/  cmake/  scripts/   documentation, CMake modules and toolchains, verification and packaging scripts
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md): modules, systems, rules of the codebase
- [Exploration pass](docs/EXPLORATION.md): audit, new systems, authoring the second demo
- [Building, testing and shipping](docs/BUILDING.md): presets, tests, macOS app and disk image, export, signing, Windows
- [Editor guide](docs/EDITOR.md): the workbench, level editing, prefabs, assets, play, export, shortcuts
- [Project format](docs/PROJECT_FORMAT.md): every file the engine reads and writes
- [Status](docs/STATUS.md): what is verified, how, and what is not done
- [Component reference](docs/components.md) (generated from the registry the Inspector uses),
  [physics API](docs/physics.md), [decisions](docs/decisions/), [the audit that started this pass](docs/AUDIT.md)
- [Dependency and font notices](THIRD_PARTY.md); the demo game's own notes are in
  [YK-DemoGame/README.md](YK-DemoGame/README.md)
