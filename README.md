# YK Engine

A proprietary C++20 2D game engine with an editor. You build a level in the editor by placing
entities, giving them components, wiring them together and pressing **Play**; the same data runs in
a standalone player.

The repository has three separate parts, and dependencies only point one way
(game module -> engine, editor -> engine):

| Part | What it is | Where |
|---|---|---|
| **Engine** | Entities and components with reflection, scene/prefab files, a headless game runtime, Box2D physics, renderer, audio, and a library of reusable gameplay components (plates, doors, hazards, a platformer controller...). No game rules. | `include/`, `src/` |
| **Editor** | Dear ImGui docking editor: hierarchy, inspector generated from reflection, scene view with move/resize/rotate gizmos, game view, assets, console, undo/redo, Play/Stop. | `editor/` |
| **Prototype game** | *Elemental Prototype*, a two-player co-op puzzle platformer used to validate the engine, with placeholder art only. Game-specific rules live in one small module. | `game/`, `projects/elemental-prototype/` |

Also here: `player/` (`yk_player`, runs any project), `tests/`, `docs/`.

## Try it

Requires CMake 3.25+, Ninja and a C++20 compiler (GCC, Clang or MSVC x64). The first configure
downloads SHA-256-pinned Box2D 3.1.1 and SDL 3.2.28 and the commit-pinned Dear ImGui 1.92.9.
Linux also needs SDL's usual development packages (X11 or Wayland, ALSA/PulseAudio).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure

./build/dev/yk_editor                                   # welcome screen: New / Open / Open Sample
./build/dev/yk_editor projects/elemental-prototype      # open the sample project directly
./build/dev/yk_player projects/elemental-prototype      # play it without the editor
```

Behind a restricted network run `scripts/fetch-deps.sh` once and set `YK_DEPS_DIR=<that folder>`
(it verifies every checkout against its recorded commit).

Playing the sample: Fire uses **A/D/W**, Water uses the **arrow keys**; both must reach their exits
(each element is safe in its own pool and deadly in the other's). **R** restarts the level.

## The editor workflow

Create or open a project, open a scene, place entities, add components, edit properties, save,
press Play, press Stop, keep editing:

1. **File > New Project** (or the welcome screen). A project is a folder with `project.ykproj`,
   `scenes/`, `prefabs/` and `assets/`.
2. **Create** entities from templates in the hierarchy's **+** menu (Platform, Door, Pressure
   Plate, Character, Spawn Point, Hazard, Goal, ...), or from prefabs.
3. **Select and shape** them in the Scene view: drag to move, **R** for resize handles, **E** to
   rotate, arrow keys to nudge, snapping to a grid.
4. **Configure** in the Inspector. Link a plate to a door with the entity field's picker, its
   eyedropper (click the door in the Scene view) or by dragging the door from the hierarchy.
5. **Ctrl+S** saves. **F5** plays a copy of the scene in the Game view (both characters, all
   mechanisms); **Shift+F5** stops and returns to editing exactly what you left.

`docs/editor.md` is the user guide. `File > Export Game` writes a folder with the player and your
project data.

## Repository layout

```text
include/yk/{core,input,scene,components,gameplay,runtime,physics,graphics,audio,assets,animation}/
src/...                   engine implementation (Box2D and SDL stay private)
game/                     prototype game module and the tool that generated its project
player/                   yk_player
editor/{core,ui}/         editor: UI-free core library, Dear ImGui panels, scripted UI driver
projects/                 sample project (data only)
tests/{unit,integration,editor}/
docs/                     architecture, editor guide, status, decisions
```

## Build options

- Presets: `dev` (Debug), `release`, `asan` (address + undefined behavior sanitizers on project
  code), `headless` (`YK_RUNTIME=OFF`: no SDL, no window; engine, gameplay, editor core and their
  tests still build).
- `format` / `format-check` targets when clang-format is installed.
- Consumers can `add_subdirectory(YK-Engine)` and link `yk::engine` (+ `yk::gameplay`). Set
  `BUILD_TESTING=OFF` for a library-only build.
- `cmake --install build/release --prefix <dir>` installs `yk_editor`, `yk_player`, the sample
  project, docs and license notices (nothing of Box2D, SDL or Dear ImGui but their notices);
  `scripts/package.sh` builds the release preset and makes `build/package/YKEngine-<version>-<system>.tar.gz`
  (a `.zip` on Windows) with CPack. The `install` test checks the installation, the installed
  programs and the archive.

## Documentation

[Editor guide](docs/editor.md), [architecture](docs/architecture.md),
[component reference](docs/components.md) (generated from the same data the inspector uses),
[physics usage](docs/physics.md), [engineering rules](docs/engineering-rules.md),
[verified status](docs/status.md), [build plan](docs/build-plan.md),
[what's next](docs/whats-next.md), [decisions](docs/decisions/), and
[dependency notices](THIRD_PARTY.md).
