# Status

What exists, how it was verified, and what is not done. Everything under "Verified" was run in the
environment this repository was developed in (Linux x86-64, GCC 13, 4 cores); anything that could
not be run there is under "Not verified". The starting point of this pass is recorded in
[AUDIT.md](AUDIT.md).

## What exists

| Area | State |
|---|---|
| **Engine core** | Entities, reflection-driven components (one declaration drives files, Inspector, prefabs, undo, docs, validation), scene/prefab/project files, headless fixed-step runtime, Box2D physics (one-way platforms, wedge ramps, triggers, kinematic carriers), SDL3 renderer (layers, view culling, tiled and nine-slice sprites, alpha and additive blending, parallax, render passes and targets), particles, glows, oscillators, audio (procedural tones and WAV). |
| **Input** | Named actions in per-player action sets stored in the project (WASD for Player1, arrows for Player2, gamepad bindings and axes), evaluated once per tick; edited in the editor. Nothing in the engine or gameplay names a key. |
| **Animation** | Clips with frame lists, per-frame timing, events and follow-up clips; a data-driven state machine (parameters, conditions, any-state transitions, exit times); sprite flipping from a parameter; previews and timing edits in the editor. Gameplay publishes generic parameters only. |
| **Gameplay library** | Platformer controller (acceleration, variable jump, coyote time, jump buffering, slopes with ground snapping, moving-platform riding), plates, levers (touch or an interact action), doors, moving platforms, hazards with tag filters, collectibles, checkpoints, spawn points, goals, trigger zones, killable with death animation and respawn, level rules with HUD variables. Camera: fixed, follow, fit targets, smoothing, zoom limits, world bounds. |
| **Editor** | VS Code-style workbench (title bar with menus and Play controls, activity bar, Explorer / Scene / Prefabs / Components / Build views, scene tabs, split editor, Inspector, Console / Problems / Build Output / Profiler panel, status bar; sizes remembered). Level editing: pan, zoom, marquee and multi-select, move/resize/rotate gizmos, snapping, duplicate, copy/paste, delete, reparent by drag, lock and hide, undo/redo, links and door-target ghosts, overlays. Inspector for entities (multi-selection edits), files (texture import settings, animation preview and timing, controllers, sounds, scenes, prefabs) and the scene's settings. Prefab instances: revert, apply, update others, unpack. Asset import (dialog and drag-and-drop) and file operations that rewrite references (rename, move, delete). Project settings: layers, input map, rendering defaults, build settings. Play/Pause/Step/Stop on a copy of the scene. Validation with a Problems panel. Export dialog. |
| **Demo game** | *Cinder Vale* in `YK-DemoGame/`: a two-player puzzle platformer with original generated art and audio, 36 prefabs, a 171-entity level and a practice room, made only of engine components and prefabs. It contains no C++. |
| **Build and packaging** | CMake presets `dev`, `release`, `asan`, `headless`, `windows-cross`; `yk export` and the Export dialog for Windows, macOS and Linux (player, data, notices, README, `Info.plist`, optional reproducible zip); install and CPack archives; the `yk` command line (`validate`, `format`, `info`, `components`, `export`, `targets`); game modules through `yk_add_game_hosts`. |
| **Documentation** | This folder: [ARCHITECTURE](ARCHITECTURE.md), [BUILDING](BUILDING.md), [EDITOR](EDITOR.md), [PROJECT_FORMAT](PROJECT_FORMAT.md), the generated [component reference](components.md), [physics API](physics.md), [decisions](decisions/). |

## Verified

Run from the last commit that changed code on this branch (documentation changed after it), by
`scripts/verify.sh dev release asan headless clang` and `scripts/verify-windows.sh`.

| Check | Result |
|---|---|
| `ctest` on `dev` (Debug, GCC 13) | 34 of 34 passed (about 6 minutes) |
| `ctest` on `release` | 34 of 34 passed (about 1.5 minutes), also from a fresh `git clone` of the pushed branch built from scratch, so nothing untracked is needed |
| `ctest` on `asan` (address + undefined behavior sanitizers, including the editor UI scripts) | 34 of 34 passed |
| `ctest` on `headless` (`YK_RUNTIME=OFF`: no SDL, no window; the tests that need none) | 17 of 17 passed |
| `ctest` on `clang` (Clang 18 and libc++, what a Mac uses; sign conversions count as errors) | 34 of 34 passed, including the demo playthrough |
| `format-check` (clang-format over the tree), `git diff --check` | clean |
| Windows: cross-compiled with MinGW-w64, every test executable run under Wine (20 of 20, including the demo playthrough, which reaches the same result as on Linux, the editor core's file operations and the game module), the demo exported for Windows by the Windows `yk.exe`, the exported `.exe` started and a frame captured | `scripts/verify-windows.sh`: everything passed |
| The installed programs work where they were put: the installed editor opens and plays the installed sample, the installed `yk` exports it with the notices found in the installation, the exported game runs | part of the `install` test |
| A game with C++ of its own: its command line validates it (the stock one refuses it), its player runs it, its export runs on its own, its editor edits and plays it | `game_module*`, `editor_game_module` |
| The engine added as a subdirectory of another project, with `yk_add_game_hosts`: validate, run, export (done once by hand, not part of the suite) | worked |
| A 41,500-entity scene (40,000 sprites in a 200 x 200 grid, 1,500 falling boxes) | Release, one 2.1 GHz Xeon core for simulation: building it takes about 20 ms, drawing a frame with the software renderer about 8 ms (39,411 of 40,000 sprites are skipped by view culling), and one physics step with 1,500 active bodies about 22 ms, so a scene that must hold 60 Hz should keep its simultaneously active bodies to a few hundred. Debug is about five times slower. |

What the test suites cover, in the order of the audit's findings:

- **The demo is completable, by a bot, through the real runtime and the real input actions**
  (`demo`: 35.2 simulated seconds, 119 checks): both characters cross the lava, the water, the
  goo and the stairs, use the lever and both plates, ride the moving platform and reach their
  exits; each dies only in the hazard that is deadly to it; restart resets the level.
- **The editor is driven through its real UI by injected mouse and keyboard events**
  (`editor_workflow`: from an empty project to an exported game; `editor_demo_edit`: links,
  marquee, resize, rotate, copy/paste, reparent, prefab save/revert/apply/update, scene tabs,
  validation; `editor_demo_play`, `editor_demo_settings`: input map and export,
  `editor_demo_assets`: Explorer, import, texture settings, animation timing, sounds, rename,
  move and delete with reference rewriting; `editor_bad_project`: a start-up project that is not
  there;
  `editor_demo_workbench`: activity bar, panel tabs, sashes, split groups, hide and lock,
  status bar; `editor_layout_*`: the layout survives a restart; `editor_game_module`: a game's
  own editor).
- **Non-visual systems have unit tests**: input maps and action evaluation, animation clips and
  controllers, scene serialization and prefab reapplication, project files and validation,
  export and zip, editor documents, selection, gizmos, layout arithmetic, asset file operations
  (moves that rewrite references, rollback, delete), physics, runtime, gameplay, effects, audio.
- **Rendering is checked against real pixels** (scene, target and render-mode tests read the
  software renderer's output).

## Not verified

- **MSVC / a native Windows build** and the editor's UI scripts on Windows: only the MinGW
  cross-build under Wine was run (tests, the exported player). MSVC builds with `/W4` but its
  warnings do not fail the build until a clean run is confirmed. The Windows executables have no
  icon or version resource and are not signed; file names outside the system's ANSI code page
  were not tried.
- **The CI workflow** (`.github/workflows/ci.yml`: Linux, Windows with MSVC, macOS) was written
  without access to those runners and has not run. It is the way to close the two items above and
  the macOS one below.
- **macOS**: the bundle export is implemented and its layout is unit-tested, but nothing was
  built or run on a Mac (no hardware; the player needs Apple's SDK). The closest check available
  is the `clang` preset (Clang and libc++ on Linux). No icon, signing or notarization.
- **High-DPI displays**: scaling is implemented from SDL's display scale but was not seen on one.
- **Real windows and GPUs**: all UI runs here used SDL's dummy video driver with the software
  renderer. Layout and behavior are exercised and screenshots reviewed, but hardware-accelerated
  presentation, native file dialogs and OS drag-and-drop of files were not exercised.
- **Gamepads**: bindings and axis handling are unit-tested with synthetic input; no physical
  controller was connected.
- **Audio output**: sound mixing is tested against SDL's dummy audio device; nothing was
  listened to.

## Known limitations

- **No scripting language.** Behavior beyond the gameplay library is a C++ component in a game
  module, and a game with such components builds its own player, editor and command line
  (`yk_add_game_hosts`). Modules are not loaded at run time.
- **Prefab instances have no override data.** Revert, apply and update replace an instance's own
  changes (except name and placement); the editor asks before bulk updates, which reach every scene
  of the project (scenes that are not open are written at once)
  ([ADR 0010](decisions/0010-prefab-instances-are-copies-with-a-source-link.md)).
- **No tilemap editor, no skeletal animation, no lighting model**, no networking. Levels are
  built from prefabs (a tiled sprite makes a resizable platform); `Light2D` is a decoration glow.
- **The Explorer has no thumbnails, no new-folder command and no drag-to-move** (Rename or Move
  takes a path, which creates the folders); the Inspector previews the selected file. File
  operations reload the open scenes, so their undo history starts again.
- **Console**: filtered by level and text, no timestamps or grouping.
- **Exported data is not packed or encrypted**: `data/` is the project's files.
- Physics and rendering limits are documented where they apply (CCD and sensor limits in
  [physics.md](physics.md); bitwise cross-platform determinism is not claimed).

## Highest-value next pass

1. **An embedded scripting language** with per-instance exposed variables, so behaviors are data
   too and games need no C++ (needs dynamic reflection: the biggest cross-cutting change).
2. **Prefab overrides**: a per-instance list of changed properties, so an update can keep them.
3. **A tilemap layer** (paint tiles from a palette, with collision), the most common thing a
   2D level author still builds by hand out of prefabs.
4. **Native builds on Windows and macOS in CI** (MSVC, Apple clang) with the editor scripts run
   on each, code signing and icons, and a player template download for cross-platform exports.
5. **Asset pipeline depth**: file operations and thumbnails in the Explorer, sprite slicing
   and clip authoring (frame picking) in the Inspector, atlas packing at export time.
