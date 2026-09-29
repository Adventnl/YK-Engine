# Project Status

Last updated: 2026-09-29. Everything here was built and run on **Linux x86_64 only** (Ubuntu 24.04,
GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, clang-format 18.1.3, Clang 18.1.3 for the extra compiler
check). Windows and macOS have not built or run this code. The previous engine-only status
(macOS arm64 and Windows results for the physics library) is in git history.

## What exists

| Part | State |
|---|---|
| **Engine** (`yk::engine`) | Scene model with reflection-declared components; scene, prefab and project files with validation; headless `GameRuntime` (physics binding, triggers, events, blackboard, restart, spawn); Box2D 3.1.1 physics behind a lifetime-safe API; SDL3 renderer with passes and render targets, scene renderer, bitmap font, procedural shapes; audio (procedural tones, WAV); animation; assets. No game rules. |
| **Gameplay library** (`yk::gameplay`) | Pressure plate, lever, door, moving platform, hazard, collectible, checkpoint, spawn point, goal, trigger zone, killable respawn, platformer controller (per-entity keys, coyote time, jump buffer, slopes, rides platforms), standard collision layers. 21 components in all (docs/components.md, generated). |
| **Prototype game** (`game/`, `projects/elemental-prototype`) | *Elemental Prototype*: two characters (Fire on WASD, Water on arrows), each element safe in its own pool and deadly in the other's, plates, levers, doors, an elevator, gems, checkpoints, exits, `LevelFlow` (win/restart/HUD). Two scenes; placeholder shapes only. A bot plays the level to completion in the test. |
| **Editor** (`yk_editor`) | Dear ImGui docking editor: welcome screen, new/open project, new/open/save scene, prefabs, hierarchy, reflection-generated inspector, scene view with move/resize/rotate gizmos, snapping, links, door/platform travel ghosts, game view, assets, console, project settings, validation, undo/redo, copy/paste/duplicate, Play/Pause/Step/Stop/Restart on a copy, Export Game. |
| **Player** (`yk_player`) | Runs any project; `--validate`, `--components`, deterministic `--fixed` runs, key scripts and frame capture for tests. |
| **Packaging** | `cmake --install`, CPack tar.gz/zip, `scripts/package.sh`; the installation holds exactly the two programs, the sample project, docs and license notices. |

About 24,000 lines of C++ (engine 9,400 including headers; editor 8,700; game and player 1,000;
tests 5,100) and 1,700 lines of documentation.

## Verification

Every gate below was run from a clean build directory on the final sources.

| Gate | Result |
|---|---|
| `dev` (Debug, GCC 13.3): clean configure, build with warnings as errors, CTest | 17/17 passed |
| `release` (GCC) | 17/17 passed |
| `asan` (AddressSanitizer + UBSan on project code and Box2D) | 17/17 passed |
| `headless` (`YK_RUNTIME=OFF`: no SDL, no window, no editor UI) | 10/10 passed (every test that needs no window) |
| Clang 18.1.3 Debug build, warnings as errors | 17/17 passed |
| `format-check` (clang-format 18), `git diff --check` | passed |
| `add_subdirectory` consumer (editor core and prototype module; Release; `YK_RUNTIME=OFF`, `BUILD_TESTING=OFF`) | configured, built, ran (the undo timing below) |
| `scripts/package.sh` | made `YKEngine-0.2.0-Linux.tar.gz` (5.4 MB, 40 files) |

The 17 tests are the 13 suites below plus three UI scripts and the install check. Configure,
build and test from an empty build directory took, on the 4-core development VM: `dev` 170 s,
`release` 152 s, `asan` 238 s, `headless` 41 s, Clang 181 s.

### Test suites

| Suite | Checks | Covers |
|---|---|---|
| `core` | passes | input bindings and edges, frame clock, camera math, movement |
| `physics` | 110 | fixed stepping and catch-up, forces, materials, sleeping, contacts and sensors, filtering, queries, joints, CCD, handle lifetime through 70,000 create/destroy cycles |
| `json` | 116 | parsing, formatting, numbers, malformed input, files |
| `animation` | 16 | clip definition and playback (once, loop) |
| `scene` | 147 | ids, hierarchy, transforms, reflection, save/load, malformed scenes, prefabs, multi-root instantiate with reference remapping |
| `game_runtime` | 126 | physics binding, triggers, layers, fixed stepping, input edges, spawn, restart, cameras, blackboard, events |
| `audio` | 9,629 | procedural tone parsing and synthesis (waveforms, invalid input) |
| `gameplay` | 180 | controller (jump, coyote, buffer, slopes, platforms, two players), plates, doors, levers, hazards, checkpoints, collectibles, goals, templates, prefabs |
| `prototype` | 77 | sample project validates; a bot completes the level (21 simulated seconds); hazards are character-specific; restart resets; `docs/components.md` is current |
| `editor_core` | 362 | undo/redo, selection, hierarchy edits, copy/paste with references, picking, move/resize/rotate/ghost interaction, snapping, project create/open/save/export, play sessions, recent projects |
| `runtime` | passes | SDL window, renderer, input and quit handling, texture ownership, real pixel readback |
| `sdl_audio` | 10 | playback voices on SDL's dummy audio device |
| `scene_render` | 40 | scenes drawn through the real renderer, pixels checked |
| `editor_workflow`, `editor_sample_play`, `editor_sample_edit` | 3 scripts | the real editor UI, below |
| `install` | 1 | installation, installed programs, CPack archive, below |

The UI scripts push real SDL mouse, keyboard and text events into the real editor (docked
windows, menus, dialogs, drag and drop, gizmos, Play), check editor and runtime state after each
step, and save screenshots. Scripts run with fixed time steps, so they behave the same on a slow or
busy machine (they also pass with a load average of eight on four cores).

- `editor_workflow`: create a project, place entities from templates, resize and move them with
  gizmos, link a plate to a door three ways, add and remove components, save, press Play and
  drive both characters with two key sets (run, jump, pause, step, restart), Stop and confirm the
  editor is as it was, undo, export the game.
- `editor_sample_play`: open the sample project, play it with both characters (Fire on A/D/W,
  Water on the arrows), inspect the running copy (read-only), Stop.
- `editor_sample_edit`: on a private copy of the sample: links and door target handles, marquee
  selection, resize with undo/redo, rotate, copy/paste/duplicate, reparent by dragging in the
  hierarchy, rename, save a prefab and place it from the Assets panel, unsaved-changes prompts,
  validation and project settings, closing the window with unsaved changes.
- `install`: install into a scratch prefix; exact contents; no Box2D/SDL/ImGui headers or libraries;
  the installed player validates and runs the installed sample; the installed editor finds it from
  the welcome screen and plays it; CPack makes the archive.

### Things the checks found and fixed this pass

- An AddressSanitizer use-after-free in `scene_tests.cpp` (a test read a child entity after
  destroying its subtree).
- `cmake --install` failed because Box2D's install rules were suppressed the wrong way; Box2D's
  directory is now excluded from installation and the `install` test guards it.
- The UI scripts were timing dependent on a busy machine (jump height, distance run); they now
  use fixed time steps.
- A truncated inspector label ("Orthographic Heig"); long names now end in "..." and the tooltip
  spells them out.
- Clang 18 rejected two things GCC accepted, both in tests: a GNU-only variadic macro in
  `tests/support/check.hpp` and `&` between booleans in the bot playthrough. Both are portable now.
- The unmeasured claim in ADR 0006 that snapshot undo is "well under a millisecond" for a few
  hundred entities was wrong; the ADR now states the measured cost (next section).

## Measured

Snapshot undo, Release build, GCC 13, the 4-core development VM. Every entity holds a sprite and a
collider; times are per committed edit, per undo and per redo. A drag or a typed edit is one change,
so this is paid once per interaction, not per frame. The sample level has 47 entities.

| Entities | Scene JSON | Edit | Undo | Redo |
|---:|---:|---:|---:|---:|
| 100 | 49 KB | 1.5 ms | 1.4 ms | 1.1 ms |
| 500 | 245 KB | 9.1 ms | 5.7 ms | 5.5 ms |
| 2,000 | 980 KB | 41 ms | 31 ms | 27 ms |
| 10,000 | 4.9 MB | 236 ms | 210 ms | 214 ms |
| 50,000 | 24.6 MB | 1.5 s | 2.4 s | 2.3 s |

## Limits and known omissions

- **Platforms.** Linux only. The code is portable C++20 over SDL3 and avoids platform APIs, but no
  Windows or macOS build, run or package has been tried, and there is no CI.
- **The GUI was verified through injected events under SDL's dummy video driver with the software
  renderer.** That exercises the real code and produced screenshots that were inspected, but no
  real window manager, GPU renderer, mouse, IME or HiDPI display was involved (HiDPI scaling is
  implemented from SDL's display scale and untested). The editor has not been driven by hand;
  there was no display to do it on.
- **Audio** ran on SDL's dummy device: mixing, tones and WAV decoding are tested, listening is not.
- **Game code stays compiled in.** The editor and player host the modules linked into them (today
  the prototype). A game with its own C++ components needs its own build; there is no plugin or
  scripting layer (whats-next.md item 2).
- **Editor.** The inspector edits the primary selection only; prefab copies are independent (no
  overrides or nesting); undo snapshots the whole scene (measured above; fine to a few thousand
  entities); no autosave or crash recovery; native file dialogs are not used (in-app pickers).
- **Runtime.** Physics poses are written back per frame from the latest tick without interpolation.
  `Collider` is box, circle or capsule (no polygons or tilemaps); no shape casts, one-way
  platforms, gamepad input or key rebinding. The prototype has a HUD but no menus or settings.
- **Physics.** As before (physics.md): single-threaded world, CCD and sensor limits, no claim of
  cross-platform bitwise determinism.
- **Assets.** PNG and BMP images and WAV sounds, copied into the project by hand; no import UI,
  sprite-sheet slicer or animation editor.
- **Dependencies** are fetched at first configure. Behind a restricted network use
  `scripts/fetch-deps.sh` and `YK_DEPS_DIR` (THIRD_PARTY.md).

API contracts: physics.md, architecture.md. Durable decisions: decisions/0001 to 0007. The
audit/remediation history of the original engine-only pass: cleanup-report.md.
