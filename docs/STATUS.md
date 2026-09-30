# Status

What exists, how it was verified, and what is not done. "Verified" lists what was actually run:
locally (Linux x86-64, GCC 13 and Clang 18, 4 cores; Windows cross-built under Wine) and on GitHub's
real runners (macOS 14 on Apple silicon, Windows Server 2022 with MSVC, Ubuntu 24.04); anything that
could not be run anywhere is under "Not verified". The starting point of the first pass is recorded
in [AUDIT.md](AUDIT.md).

## What exists

| Area | State |
|---|---|
| **Engine core** | Entities, reflection-driven components (one declaration drives files, Inspector, prefabs, undo, docs, validation and what a component says is wrong with itself), scene/prefab/project files, headless fixed-step runtime with frame-time snapping, screen fades and input locks, `GameSession` (the one place scenes are switched, shared by the player and the editor), Box2D physics (one-way platforms, wedge ramps, rounded and chamfered colliders, triggers, kinematic carriers that carry by the velocity of the point under each rider, hinge joints with limits, spring and motor, bullets, collision enter and exit), SDL3 renderer (layers, view culling, tiled and nine-slice sprites, alpha and additive blending, parallax, render passes and targets), particles, glows, oscillators, audio (procedural tones and WAV). |
| **Input** | Named actions in per-player action sets stored in the project (WASD for Player1, arrows for Player2, gamepad bindings and axes), evaluated once per tick; edited in the editor. Nothing in the engine or gameplay names a key. |
| **Animation** | Clips with frame lists, per-frame timing, events and follow-up clips; a data-driven state machine (parameters, conditions, any-state transitions, exit times); sprite flipping from a parameter; previews and timing edits in the editor. Gameplay publishes generic parameters only. |
| **Gameplay library** | Platformer controller (acceleration, variable jump, coyote time, jump buffering, slopes with ground snapping, riding sliding, rotating, tilting and sinking surfaces), **physical pressure plates** (a kinematic pad characters and crates stand on and sink with, weight or region sensing), levers (touch or an interact action), doors that slide and/or turn, eased and spinning moving platforms (all stop when something is squeezed), hazards with tag filters, collectibles, checkpoints, spawn points, goals (exits that whoever stands in walks into), trigger zones, killable with death animation and respawn, **`LevelFlow`** (intro, time limit, failure and retry, completion sequence with locked input, continue, next scene, variables carried over) and **`EventAction`** (events to consequences, with delays and timers). Camera: fixed, follow, fit targets, smoothing, zoom limits, world bounds. |
| **Editor** | VS Code-style workbench drawn as outlined cards on a dark canvas like the reference (title bar with menus and a Play/project capsule, activity bar + side bar with Explorer / Scene / Prefabs / Components / Build views and an Open Scenes list, scene tabs, split editor, right card with **Inspector** and **Debug** tabs (live state of the running game), Console / Problems / Build Output / Profiler panel, status bar; every region resizable and collapsible, sizes remembered; Cmd shortcuts on a Mac; `--ui-scale`). Level editing: pan, zoom (buttons, a menu of round levels, keys, wheel about the pointer, fit and focus; a narrow group folds the toolbar into a "..." menu), marquee and multi-select, move/resize/rotate gizmos, draggable pins (a hinge's anchor), snapping, duplicate, copy/paste, delete, reparent by drag, lock and hide, undo/redo, links and door-target ghosts, overlays, a right-click menu; the Problems panel selects the entity a finding is about. Inspector for entities (multi-selection edits), files (texture import settings, animation preview and timing, controllers, sounds, scenes, prefabs) and the scene's settings. Prefab instances: revert, apply, update others, unpack. Asset import (dialog and drag-and-drop) and file operations that rewrite references (rename, move, delete). Project settings: layers, input map, rendering defaults, build settings. Play/Pause/Step/Stop on a copy of the scene. Validation with a Problems panel. Export dialog. |
| **Demo game** | *Cinder Vale* in `YK-DemoGame/`: a two-player puzzle platformer with original generated art and audio, 36 prefabs, a 173-entity level and a practice room that lead into each other (completing one fades into the other), made only of engine components and prefabs. Its plates are real buttons. It contains no C++. |
| **Application lifecycle** | Per-user log files with rotation, a crash reporter (report + stack trace, next start says so), a running marker for unclean-exit detection, `SIGPIPE`/`SIGHUP` ignored, fatal start-up errors shown in a dialog (never a silent exit), OS-asked executable and bundle paths (nothing depends on the working directory or the source tree), the editor tracks the player it starts and ends it on quit, **Help > Open Logs Folder**. |
| **macOS distribution** | `scripts/package-macos.sh` builds **`YK Engine.app`** (editor, player, `yk`, icon, demo game, notices, `.ykproj` document type) and **`YKEngine-<version>-macos-<arch>.dmg`**, signs ad hoc or with a Developer ID (`YK_CODESIGN_IDENTITY`, hardened runtime) and optionally notarizes and staples (`YK_NOTARY_PROFILE`); `scripts/verify-macos-app.sh` checks the result like a user would. |
| **Export pipeline** | `yk export` and the Export dialog write Windows, macOS and Linux games: player renamed after the game, data, notices, README, `Info.plist`, project icon (`AppIcon.icns` from a PNG), copyright, optional reproducible zip; on a Mac also code signing and a `.dmg` (`--sign`, `--dmg`). The exported game has no editor. |
| **Build and packaging** | CMake presets `dev`, `release`, `asan`, `headless`, `clang`, `windows-cross`; install and CPack archives; the `yk` command line (`new`, `validate`, `format`, `info`, `components`, `export`, `targets`); game modules through `yk_add_game_hosts`; a game can live in its own repository (`yk new`, `YK_DEMO_PROJECT`). |
| **Documentation** | This folder: [ARCHITECTURE](ARCHITECTURE.md), [BUILDING](BUILDING.md), [EDITOR](EDITOR.md), [PROJECT_FORMAT](PROJECT_FORMAT.md), the generated [component reference](components.md), [physics API](physics.md), [decisions](decisions/). |

## The latest pass: mechanisms, level flow and getting around the editor

A production-readiness pass over an engine that already worked, in the order it was done. The last
column names what would fail without the change.

| Area | What changed | Checked by |
|---|---|---|
| **Pressure plates** | A plate is a kinematic pad with a solid collider: characters and crates land on it, stand on it and sink with it, it stops at an exact depth and rises when the load leaves. Weight sensing reads the pad's own contacts; a trigger region is a separate option. A chamfered rim lets a crate be pushed up onto it. Pressed at 85% of the travel, released below 70%. ([ADR 0013](decisions/0013-pressure-plates-are-solid-pads-that-carry-their-load.md)) | `mechanisms` (a character dropped from a height, walking over, standing, jumping again and again; a crate; a stack; the stops; tags and minimum mass; region plates), `demo` |
| **Moving, rotating and tilting surfaces** | Characters ride the velocity of the ground *point* (linear, angular and centripetal), so sliding, spinning, tilting and sinking surfaces carry them. Platforms and doors ease in and out and hold still when something is squeezed. Characters are bullets (continuous collision against kinematic bodies). New: `Door.openRotation`, `MovingPlatform.spinSpeed`, `HingeJoint` (limits, spring, motor), `Collider.cornerRadius` and `chamfer`, `World::pointVelocity`, `setBullet`, `onCollisionExit`. ([ADR 0014](decisions/0014-characters-ride-the-velocity-of-the-ground-point.md)) | `mechanisms` (elevators, platforms that start downward, gates and platforms that do not crush, rotating platforms, angled surfaces, a hinged seesaw and door, crates as solid props, fast things not tunneling, collision enter and exit, the frame rate not changing the result) |
| **Level flow** | `LevelFlow` is a state machine (intro, playing, complete, failed) with goals, a time limit, retry, continue, the next scene and variables carried over; `Goal` exits are walked into; controls lock while a level ends; restarts and scene changes fade; `GameSession` is the one place scenes are switched, for the player and the editor's Play alike; `EventAction` connects events to consequences with delays and timers; the pause action works. ([ADR 0015](decisions/0015-level-flow-and-transitions-belong-to-the-runtime.md)) | `level_flow`, `demo` (room 1, the practice room, room 1 again through a session) |
| **Validation** | A component can say what is wrong with itself (a plate with nothing to stand on, a lever with no trigger, a hinge on a body that cannot swing, a next scene that is not in the project); the Problems panel goes to the entity. | `mechanisms`, `editor_demo_edit` |
| **Scene view** | Zoom out / level menu / zoom in / fit / focus on the toolbar, Ctrl+= Ctrl+- Ctrl+0, a sideways wheel pans, a narrow group folds the toolbar into a menu, draggable pins for hinge anchors, a context menu with stable ids. Works while the game plays. | `editor_core` (the zoom ladder, pins), `editor_demo_view` (all of it through the real UI) |
| **Fewer dead controls** | The Game tab's Restart is disabled until a game runs; Move Up and Move Down in the Hierarchy and the Entity menu are offered only when they move something. | `editor_core`, `editor_demo_edit` |
| **Timing and camera** | A frame within 0.4 ms of a tick (or half, two, three) counts as exact, so a 60 or 120 Hz screen does not turn its uneven frames into a hitch; the camera follows once per tick. | `game_runtime` |
| **Save and load** | Every registered component keeps every editable field (compared field by field with non-default values); a scene written before the newer fields existed loads with their defaults and plays as it did. | `gameplay` |
| **Demo** | Plates are real buttons (a root and a `Pad`); room 1 and the practice room lead into each other; P pauses, Enter continues. | `demo`, `editor_demo_*` |

## Verified

### On real runners (GitHub Actions, `.github/workflows/ci.yml`)

CI run 29, commit `40e47b5` (the last commit that changes code, tests or scripts; a later one only
corrects this document): all three jobs green, as they were in run 28 on `c584052`, the same code
without the pause step in `demo_play.ykscript`. Getting there took three earlier runs, which are part
of the record: two failed on the editor scripts whose expected entity counts had not followed the
plates that now have a pad (`editor_demo_edit`, `editor_demo_assets`; fixed), and one on macOS alone,
where Apple clang rejected a size-to-signed conversion in a new test that GCC and MSVC accept
(`-Wsign-conversion`; fixed, and Clang 18 now builds the whole tree without a warning).

| Runner | Result |
|---|---|
| **macOS 14, Apple silicon, Apple clang, `release`** | Build clean (warnings are errors). Full test suite passed, **including all editor UI scripts in a real Mac build**. `scripts/package-macos.sh` produced `YK Engine.app` and the `.dmg`; `scripts/verify-macos-app.sh` passed: the image mounts and holds the app and an Applications link, the app copied to a path with a space verifies its signature, `iconutil` accepts `AppIcon.icns`, a game exported with `--dmg` and an ad hoc signature verifies and its `.dmg` mounts and holds the app, the exported game started through Launch Services (`open`) from another folder runs, writes `~/Library/Logs/CinderVale/player.log` and closes cleanly, and the editor opens a `.ykproj` document through Launch Services and quits on request with a clean shutdown and no process left. The `.dmg`, screenshots and logs are kept as the run's artifact `macos-engine`. |
| **Windows Server 2022, MSVC (VS 2022, x64), Release** | Build clean; every registered test passed (the whole suite except `macos_bundle` and `diagnostics_crash`, which do not exist on Windows), including all editor UI scripts, `external_project` and the install/CPack test. |
| **Ubuntu 24.04, GCC, `dev` (Debug)** | clang-format check, build and all 42 tests passed (about 20 minutes of tests in Debug). |

### Locally

| Check | Result |
|---|---|
| `ctest` on `dev` (Debug, GCC 13) | 42 of 42 passed (6 minutes with three tests at a time) |
| `ctest` on `release` | 42 of 42 passed (4 minutes) |
| `ctest` on `asan` (address + undefined behavior sanitizers, including the editor UI scripts, the crash test and the bundle test) | 42 of 42 passed (21 minutes) |
| `ctest` on `headless` (`YK_RUNTIME=OFF`: no SDL, no window; the tests that need none) | 20 of 20 passed |
| Clang 18 (with libstdc++), `release` | the whole tree builds with every warning an error and none raised; the `clang` preset itself (libc++) is not available on this machine, see "Not verified" |
| `format-check` (clang-format over the tree), `git diff --check` | clean (`scripts/verify.sh`) |
| Windows cross-build (MinGW-w64) under Wine | passed in the earlier pass (`scripts/verify-windows.sh`); not re-run in this one, the native MSVC run above replaces it |
| The standalone player | started with the demo project under the software renderer for 90 frames and captured (the level, the HUD, the characters); again with a scripted P press (the picture dims and says PAUSED). Nobody played it. |
| The installed programs work where they were put | part of the `install` test |
| A game with C++ of its own | `game_module*`, `editor_game_module` |
| A 41,500-entity scene (40,000 sprites, 1,500 falling boxes) | Release, one 2.1 GHz Xeon core: building about 20 ms, drawing a frame about 8 ms (view culling skips 39,411 of 40,000 sprites), one physics step with 1,500 active bodies about 22 ms; a scene that must hold 60 Hz should keep its simultaneously active bodies to a few hundred. (Measured in an earlier pass; the `stress` test still passes its budgets.) |

What the newest tests cover:

- **`mechanisms`** (947 checks): real physics and real input against the mechanisms. A character
  dropped onto a plate from several heights, walking over one, standing on one, jumping on it again
  and again; a crate pushed up onto a plate, a stack on it; the plate's stops and how it presses and
  releases; elevators, platforms that start downward, gates and platforms that hold still when
  something is in the way, rotating platforms and angled surfaces carrying riders, a hinged seesaw
  and door, crates as solid props, fast things that must not tunnel, collision enter and exit, the
  same result at 20, 60 and 144 frames a second, and the messages validation gives.
- **`level_flow`** (116 checks): the intro, the completion sequence (input locked, both characters
  walk into their exits and vanish, the level moves on after its delay), failure and retry, the time
  limit, the fade between scenes and the session that follows a scene change (a scene that cannot be
  loaded leaves the game where it was), variables carried over, reactions to events with delays,
  chains and timers, and the pause key.
- **`gameplay`** (356 checks): also every registered component saved and loaded field by field, and a
  scene written before the newer fields existed.
- **`game_runtime`** (384 checks): also the frame-time snapping and the camera following per tick.
- **`editor_core`** (616 checks): also the zoom ladder and levels, pins, and which sibling moves are
  offered.
- **`editor_demo_view`**: zoom out, in, the level menu, fit, focus, the keys and the View menu, the
  wheel, panning with Space and a drag, dragging a hinge's pin, the right-click menu, the folded
  toolbar in a split editor, all of it while the game plays too. **`editor_demo_edit`** also goes to
  the entity a problem is about and picks a next scene; **`editor_demo_play`** also pauses the game
  with its own key.
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
real runtime and input actions (`demo`, 42.0 simulated seconds, 136 checks, and then through the two rooms); the editor is driven
through its real UI by injected mouse and keyboard events (`editor_workflow` from an empty project to
an exported game, `editor_demo_edit/play/settings/assets/workbench/view`, `editor_layout_*`,
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
  dummy devices; no controller was connected and nothing was listened to (the new `Continue` and
  `Pause` actions have pad bindings, East and Start, that no pad has pressed).
- **How the game and the new editor controls feel on a real display.** The mechanisms, the level
  flow and the scene view were exercised headlessly (real physics, scripted input, real UI events
  under the software renderer), and the standalone player was started and captured (the level, then
  the paused overlay). Nobody played the demo with a keyboard, moved the mouse wheel over the scene
  view, or looked at the plates and platforms at a display's own refresh rate. Rendering the whole
  editor at high zoom levels is slow under the software renderer, so the UI scripts stay at 100% and
  below; a GPU does not have that problem, but it was not tried.
- **The `clang` preset** (Clang with libc++) could not run on the machine used for this pass (no
  libc++). Clang 18 with libstdc++ built everything without a warning, and the macOS job on GitHub
  (Apple clang and libc++) is the check that stands in for it.

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
- **Motion is drawn at the simulation rate.** Nothing is interpolated between fixed ticks, so on a
  display faster than 60 Hz the picture changes 60 times a second (a 120 Hz screen shows each tick
  twice). Frame times are snapped to the tick and the camera follows per tick, so nothing shimmers
  against anything else, but it is not 144 fps motion. Interpolating costs a tick of latency; it is
  the next step for a game that wants it.
- **Mechanisms notice things within the solver's speculative margin (about 2 cm).** A door or plate
  can overlap what it presses on or crushes by a few centimeters before it knows, and the tests allow
  for that. A step higher than about 15 cm is a wall to the character controller (its slope limit
  is 55 degrees) unless it has a chamfer or a ramp, which is why plates have a sloped rim.
- **The scene view draws a hinge's pin but not its swing, and a hinged door's open pose is edited as
  a number** (the ghost of a door that only slides is drawn and draggable).
- **`EventAction` has no conditions** (this event AND that one): anything a chain of events,
  delays and signals cannot say is a component in a game module, as before.
- **Ctrl+= and Ctrl+-** zoom the scene view on keyboards where "=" and "-" are keys; SDL 3.2 reports
  no trackpad pinch gesture, so there is no pinch-to-zoom (a two-finger scroll zooms and a sideways
  swipe pans; the toolbar's buttons and menu are always there).
- Physics and rendering limits are documented where they apply (CCD and sensor limits in
  [physics.md](physics.md); bitwise cross-platform determinism is not claimed).

## Highest-value next pass

1. **Render interpolation** for displays faster than 60 Hz (see the limitation above), together
   with a tilted preview of a hinged door's open pose and the swing range of a hinge in the scene
   view.
2. **Notarized releases**: run `scripts/package-macos.sh` with a Developer ID and a notary profile
   in CI secrets, staple, and verify with `spctl` on a clean Mac; add a universal (arm64 + x86_64)
   build. This is the step between "works on the developer's Mac" and "opens on anyone's Mac".
3. **See it on a Retina display** and fix what only shows there (icon sizes, 1-pixel borders,
   font weight); add screenshot review of the CI artifact to the release checklist.
4. **An embedded scripting language** with per-instance exposed variables, so behaviors are data
   too and games need no C++ (needs dynamic reflection: the biggest cross-cutting change).
5. **Prefab overrides** and **a tilemap layer**, the two things a 2D level author still works
   around by hand.
6. **Editor recovery**: periodic snapshots of unsaved scenes so a crash does not lose work, and
   Windows crash-report verification plus an icon/version resource for exported `.exe` files.
