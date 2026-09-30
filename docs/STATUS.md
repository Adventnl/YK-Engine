# Status

What exists, how it was verified, and what is not done. "Verified" lists what was actually run:
locally (Linux x86-64, GCC 13 and Clang 18, 4 cores; Windows cross-built under Wine) and on GitHub's
real runners (macOS 14 on Apple silicon, Windows Server 2022 with MSVC, Ubuntu 24.04); anything that
could not be run anywhere is under "Not verified". The starting point of the first pass is recorded
in [AUDIT.md](AUDIT.md).

## What exists

| Area | State |
|---|---|
| **Engine core** | Entities, reflection-driven components (one declaration drives files, Inspector, prefabs, undo, docs, validation), scene/prefab/project files, headless fixed-step runtime, Box2D physics (one-way platforms, wedge ramps, triggers, kinematic carriers), SDL3 renderer (layers, view culling, tiled and nine-slice sprites, alpha and additive blending, parallax, render passes and targets), particles, glows, oscillators, audio (procedural tones and WAV). |
| **Input** | Named actions in per-player action sets stored in the project (WASD for Player1, arrows for Player2, gamepad bindings and axes), evaluated once per tick; edited in the editor. Nothing in the engine or gameplay names a key. |
| **Animation** | Clips with frame lists, per-frame timing, events and follow-up clips; a data-driven state machine (parameters, conditions, any-state transitions, exit times); sprite flipping from a parameter; previews and timing edits in the editor. Gameplay publishes generic parameters only. |
| **Gameplay library** | Platformer controller (acceleration, variable jump, coyote time, jump buffering, slopes with ground snapping, moving-platform riding), plates, levers (touch or an interact action), doors, moving platforms, hazards with tag filters, collectibles, checkpoints, spawn points, goals, trigger zones, killable with death animation and respawn, level rules with HUD variables. Camera: fixed, follow, fit targets, smoothing, zoom limits, world bounds. |
| **Editor** | VS Code-style workbench drawn as outlined cards on a dark canvas like the reference (title bar with menus and a Play/project capsule, activity bar + side bar with Explorer / Scene / Prefabs / Components / Build views and an Open Scenes list, scene tabs, split editor, right card with **Inspector** and **Debug** tabs (live state of the running game), Console / Problems / Build Output / Profiler panel, status bar; every region resizable and collapsible, sizes remembered; Cmd shortcuts on a Mac; `--ui-scale`). Level editing: pan, zoom, marquee and multi-select, move/resize/rotate gizmos, snapping, duplicate, copy/paste, delete, reparent by drag, lock and hide, undo/redo, links and door-target ghosts, overlays. Inspector for entities (multi-selection edits), files (texture import settings, animation preview and timing, controllers, sounds, scenes, prefabs) and the scene's settings. Prefab instances: revert, apply, update others, unpack. Asset import (dialog and drag-and-drop) and file operations that rewrite references (rename, move, delete). Project settings: layers, input map, rendering defaults, build settings. Play/Pause/Step/Stop on a copy of the scene. Validation with a Problems panel. Export dialog. |
| **Demo game** | *Cinder Vale* in `YK-DemoGame/`: a two-player puzzle platformer with original generated art and audio, 36 prefabs, a 171-entity level and a practice room, made only of engine components and prefabs. It contains no C++. |
| **Application lifecycle** | Per-user log files with rotation, a crash reporter (report + stack trace, next start says so), a running marker for unclean-exit detection, `SIGPIPE`/`SIGHUP` ignored, fatal start-up errors shown in a dialog (never a silent exit), OS-asked executable and bundle paths (nothing depends on the working directory or the source tree), the editor tracks the player it starts and ends it on quit, **Help > Open Logs Folder**. |
| **macOS distribution** | `scripts/package-macos.sh` builds **`YK Engine.app`** (editor, player, `yk`, icon, demo game, notices, `.ykproj` document type) and **`YKEngine-<version>-macos-<arch>.dmg`**, signs ad hoc or with a Developer ID (`YK_CODESIGN_IDENTITY`, hardened runtime) and optionally notarizes and staples (`YK_NOTARY_PROFILE`); `scripts/verify-macos-app.sh` checks the result like a user would. |
| **Export pipeline** | `yk export` and the Export dialog write Windows, macOS and Linux games: player renamed after the game, data, notices, README, `Info.plist`, project icon (`AppIcon.icns` from a PNG), copyright, optional reproducible zip; on a Mac also code signing and a `.dmg` (`--sign`, `--dmg`). The exported game has no editor. |
| **Build and packaging** | CMake presets `dev`, `release`, `asan`, `headless`, `clang`, `windows-cross`; install and CPack archives; the `yk` command line (`new`, `validate`, `format`, `info`, `components`, `export`, `targets`); game modules through `yk_add_game_hosts`; a game can live in its own repository (`yk new`, `YK_DEMO_PROJECT`). |
| **Documentation** | This folder: [ARCHITECTURE](ARCHITECTURE.md), [BUILDING](BUILDING.md), [EDITOR](EDITOR.md), [PROJECT_FORMAT](PROJECT_FORMAT.md), the generated [component reference](components.md), [physics API](physics.md), [decisions](decisions/). |

## Verified

### On real runners (GitHub Actions, `.github/workflows/ci.yml`)

Latest runs on the branch: run 12 (commit `b3735b1`) and run 13 (`b98af85`).

| Runner | Result |
|---|---|
| **macOS 14, Apple silicon, Apple clang, `release`** | Build clean (warnings are errors). Full test suite passed, **including all editor UI scripts in a real Mac build** (the first run there failed 8 of them: Dear ImGui swaps Control and Command on macOS; fixed, see [EDITOR.md](EDITOR.md#keyboard-shortcuts)). `scripts/package-macos.sh` produced `YK Engine.app` and the `.dmg`; `scripts/verify-macos-app.sh` passed: the image mounts and holds the app and an Applications link, the app copied to a path with a space verifies its signature, `iconutil` accepts `AppIcon.icns`, a game exported with `--dmg` and ad hoc signature verifies and its `.dmg` mounts, the exported game started through Launch Services (`open`) from another folder runs, writes `~/Library/Logs/<game>/player.log` and shuts down cleanly when asked to quit, and the editor opens a `.ykproj` document through Launch Services and quits cleanly. The `.dmg`, screenshots and logs are kept as the run's artifact `macos-engine`. |
| **Windows Server 2022, MSVC (VS 2022, x64), Release** | Build clean; 36 of 37 tests passed, including every editor UI script, `external_project` and the install/CPack test. The one failure, `demo`, was the generated component reference compared byte for byte after Git turned it into CRLF on checkout; fixed with a `.gitattributes` (LF everywhere), which the next run confirms (see below). `macos_bundle` and `diagnostics_crash` are not registered on Windows. |
| **Ubuntu 24.04, GCC, `dev` (Debug)** | Format check, build and full suite passed on earlier runs; see the latest run for the current commit. |

### Locally

| Check | Result |
|---|---|
| `ctest` on `release` (39 tests) | all passed (the two that failed once did so because I had started a second `ctest` on the same tree at the same time; run alone they pass) |
| `scripts/verify.sh headless clang asan dev` after the last code change | see the note at the end of this section |
| `format-check` (clang-format over the tree), `git diff --check` | clean |
| Windows cross-build (MinGW-w64) under Wine (previous pass; the Windows-specific code changed little since) | `scripts/verify-windows.sh`: everything passed |
| The installed programs work where they were put | part of the `install` test |
| A game with C++ of its own | `game_module*`, `editor_game_module` |
| A 41,500-entity scene (40,000 sprites, 1,500 falling boxes) | Release, one 2.1 GHz Xeon core: building about 20 ms, drawing a frame about 8 ms (view culling skips 39,411 of 40,000 sprites), one physics step with 1,500 active bodies about 22 ms; a scene that must hold 60 Hz should keep its simultaneously active bodies to a few hundred. |

What the newest tests cover:

- **`diagnostics`**: paths (bundle detection, per-system folders, `YK_LOG_DIR`), log rotation and
  flushing, the running marker and unclean-exit detection; real child processes crash by `abort`,
  uncaught exception and invalid memory access, and the report and the next start's notice are read.
- **`diagnostics_crash`**: the real editor crashes on purpose, leaves a report and log, and the next
  editor start shows the notice (driven through the UI).
- **`macos_bundle`**: `YK Engine.app` is assembled in a folder the programs were never built in
  (name with a space) and used from there: `yk` finds the player and notices, an export gets the
  macOS layout and its program runs from a third folder, the editor finds the sample in `Resources`.
- **`external_project`**: a new project and a copy of the demo in a temporary folder with a space and
  an umlaut are validated, played and exported from other working directories; the export runs
  from a fourth and logs where logs go.
- **Export tests**: icons and `.icns`, the exact `codesign` and `hdiutil` command lines through a
  recording runner, a real child process, validation of a bad icon.
- **`editor_demo_player`**: **Run in Player** starts the real player, the status bar shows it, Stop
  ends it, and no orphan process is left (checked with `pgrep`).
- **`editor_demo_workbench`**: the Debug tab, the project capsule menu, the side bar menu, the split
  button, Open Scenes.

The earlier suites are unchanged in what they cover: the demo is completable by a bot through the
real runtime and input actions (`demo`, 35.2 simulated seconds, 119 checks); the editor is driven
through its real UI by injected mouse and keyboard events (`editor_workflow` from an empty project to
an exported game, `editor_demo_edit/play/settings/assets/workbench`, `editor_layout_*`,
`editor_bad_project`, `editor_game_module`); non-visual systems have unit tests (input, animation,
serialization, projects, validation, export and zip, editor documents, selection, gizmos, layout
arithmetic including the cards, file operations, physics, runtime, gameplay, effects, audio);
rendering is checked against real pixels.

## Not verified

- **Notarization and Developer ID signing.** The code path exists (`YK_CODESIGN_IDENTITY`,
  `YK_NOTARY_PROFILE`, hardened runtime, entitlements) but needs an Apple Developer account, which
  was not available; only the ad hoc path was run. An app or `.dmg` from the ad hoc build shows
  Gatekeeper's warning on another Mac (Control-click > Open once).
- **Universal or Intel macOS builds.** CI builds for Apple silicon only.
- **High-DPI displays.** Scaling follows SDL's display scale (and `--ui-scale`), but the interface
  was never looked at on a Retina display, and the screenshots the macOS job keeps were not viewed
  (the environment this was developed in cannot download run artifacts).
- **Real GPUs and native file dialogs.** UI runs use SDL's dummy video driver and the software
  renderer. The macOS job additionally opens a real window through Launch Services, but nobody
  looked at hardware-accelerated presentation, native open/save dialogs or OS drag-and-drop.
- **Windows crash reporting** compiles and is hooked in, but has not been run (the crash test is
  POSIX-only). The exported Windows `.exe` has no icon or version resource and is not signed.
- **Linux desktop integration.** No `.desktop` file, icon theme entry or AppImage; a Linux export is
  a folder or zip.
- **Gamepads and audio output**: bindings and mixing are tested with synthetic input and SDL's
  dummy devices; no controller was connected and nothing was listened to.

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
- **Console**: filtered by level and text, no timestamps or grouping (the log file has timestamps).
- **No auto-save or crash recovery of unsaved scenes**: after a crash the editor reports it and names
  the log and report, but unsaved changes are lost.
- **The editor cannot notarize** an exported game and does not manage signing identities; the export
  dialog signs with an identity you type (or ad hoc).
- **Exported data is not packed or encrypted**: `data/` is the project's files.
- Physics and rendering limits are documented where they apply (CCD and sensor limits in
  [physics.md](physics.md); bitwise cross-platform determinism is not claimed).

## Highest-value next pass

1. **Notarized releases**: run `scripts/package-macos.sh` with a Developer ID and a notary profile
   in CI secrets, staple, and verify with `spctl` on a clean Mac; add a universal (arm64 + x86_64)
   build. This is the step between "works on the developer's Mac" and "opens on anyone's Mac".
2. **See it on a Retina display** and fix what only shows there (icon sizes, 1-pixel borders,
   font weight); add screenshot review of the CI artifact to the release checklist.
3. **An embedded scripting language** with per-instance exposed variables, so behaviors are data
   too and games need no C++ (needs dynamic reflection: the biggest cross-cutting change).
4. **Prefab overrides** and **a tilemap layer**, the two things a 2D level author still works
   around by hand.
5. **Editor recovery**: periodic snapshots of unsaved scenes so a crash does not lose work, and
   Windows crash-report verification plus an icon/version resource for exported `.exe` files.
