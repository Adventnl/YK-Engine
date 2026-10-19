<p align="center"><img src="packaging/icons/YKEngine-256.png" alt="YK Engine logo" width="160"></p>

# YK Engine

A C++20 2D game engine and editor. Build scenes from reusable components, press **F5** to play,
and export a standalone desktop game. The editor, player and command line share the same runtime.

The repository contains exactly two independent games:

| Game | Project | Play |
|---|---|---|
| **Fireboy and Watergirl** | [games/fireboy-watergirl](games/fireboy-watergirl/README.md) | Title screen, level selection and two co-op platforming levels. |
| **Blackthorn Keep** | [games/blackthorn-keep](games/blackthorn-keep/README.md) | Two castle escape chapters with NPCs, inventories, searches, crafting and patrols. Title screen, chapter selection, controls and pause menu. |

All scenes, assets, dialogue, game text, original art imports, authoring scripts and game-specific
checks belong under `games/`. Engine code in `src/` and `include/` supplies reusable systems;
it has no dependency on either game. The editor discovers ordinary `project.ykproj` files under
`games/` and lists their names on its welcome screen.

## Build and play

Requires CMake 3.25+, Ninja and a C++20 compiler. The first configure downloads pinned dependencies.
See [Building](docs/BUILDING.md) for Windows, macOS, dependency setup and packaging.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure

./build/dev/yk_editor
./build/dev/yk_player games/blackthorn-keep
./build/dev/yk_player games/fireboy-watergirl
```

Open either project from the welcome screen and press **F5** to play in the editor.
Blackthorn Keep starts at its title screen. Choose either chapter with the mouse or **1 / 2**.
Use **WASD / arrows** to move, **E** to interact, **Escape** to close a popup or pause,
**P** to pause, **R** to restart, **M** for the main menu and **Tab** for chapter selection. Fireboy uses **A/D/W** and Watergirl uses the **arrow keys**;
its existing title screen and level selection are preserved.

For an engine build without bundled games or their checks:

```sh
cmake -S . -B build/engine-only -G Ninja -DYK_BUILD_GAMES=OFF
cmake --build build/engine-only
ctest --test-dir build/engine-only --output-on-failure
```

The engine also supports projects anywhere outside this repository:

```sh
./build/dev/yk new ~/Games/MyGame --name "My Game"
./build/dev/yk_editor ~/Games/MyGame
./build/dev/yk export games/blackthorn-keep --target macos --out dist --zip
```

## Download and install

[GitHub Releases](https://github.com/Adventnl/YK-Engine/releases) provides Windows x64 and macOS
installers. Downloads include the editor, player, command line, games and dependency notices.
No compiler is needed to play or edit. Installed editors check for release updates on startup.

## Repository layout

```text
include/yk/  src/         reusable engine and gameplay systems
editor/                  editor core and interface
player/  tools/yk/        generic player and project command line
games/                  independent game projects and their checks
tests/                  engine tests and small synthetic fixtures
packaging/  cmake/       distribution and build support
docs/  scripts/          documentation, build and verification scripts
```

- [Architecture](docs/ARCHITECTURE.md)
- [Building and packaging](docs/BUILDING.md)
- [Editor guide](docs/EDITOR.md)
- [Project format](docs/PROJECT_FORMAT.md)
- [Component reference](docs/components.md)
- [Dependency notices](THIRD_PARTY.md)
