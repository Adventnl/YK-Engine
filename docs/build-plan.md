# Build Plan

## Goal

A proprietary 2D game engine with an editor, proven by a small game. The engine and editor are the
product; the game (*Elemental Prototype*, a two-player co-op puzzle platformer in the spirit of
Fireboy and Watergirl, placeholder art only) exists to show that the engine can express a real game
end to end: create a project, place entities, add components, configure them, save, press Play,
play, press Stop, keep editing.

Three parts stay separate (architecture.md, ADR 0004): the engine and its reusable gameplay library
know no game; the editor and the standalone player host the modules they are given; the prototype
game module holds only rules specific to the prototype. Dependencies point one way.

## Layout

```text
CMakeLists.txt  CMakePresets.json  README.md  THIRD_PARTY.md  .clang-format
include/yk/{core,input,scene,components,gameplay,runtime,physics,graphics,audio,assets,animation}/
src/...                   engine and gameplay library implementation (Box2D, SDL stay private)
game/                     prototype game module, and the tool that generated its project
player/                   yk_player, the standalone runner
editor/{core,ui}/         yk_editor: UI-free core library; Dear ImGui panels and scripted driver
projects/                 the sample project (data only)
tests/{unit,integration,editor,support}/
docs/  docs/decisions/  LICENSES/  third_party/  scripts/
build/                    ignored generated output
```

Create only what is used: no empty folders, forwarding layers or speculative interfaces.

## Milestones

Evidence for each completed item is in status.md; unmet limits are in whats-next.md.

- [x] **Physics library.** Root layout, private SHA-256-pinned Box2D, RAII world, lifetime-safe
  handles, shapes, materials, filtering, fixed stepping, copied events, queries, joints, debug
  outlines, headless build. (The original engine-only milestone; its limits are in physics.md.)
- [x] **Scene and reflection.** Entities with ids, hierarchy and tags; components declared once
  through a registry; scene, prefab and project files with validation; generated component docs
  (ADR 0005). The earlier hard-coded tile-map demo was retired from the engine core.
- [x] **Headless runtime.** `GameRuntime` binds scenes to physics, runs fixed ticks and component
  updates, triggers, events, a blackboard, deferred destroy, spawn, restart and scene changes. The
  player, the editor's Play mode and the tests run the same code.
- [x] **Rendering and audio from scene data.** Multi-pass renderer with render targets, scene
  renderer (sprites, procedural shapes, screen-space UI, bitmap font, collider outlines), game view,
  animation, procedural and WAV audio.
- [x] **Gameplay library.** Signals, pressure plates, levers, doors, moving platforms, hazards,
  collectibles, checkpoints, spawn points, goals, trigger zones, killable respawn, a data-driven
  platformer controller, standard collision layers.
- [x] **Prototype game and sample project.** `LevelFlow` and character/exit/pool templates in the
  game module; a two-character sample project with two scenes; a scripted bot playthrough test that
  fails when the level cannot be completed.
- [x] **Editor core.** Document with change-based undo/redo, selection, hierarchy operations,
  picking and gizmo geometry derived from reflection, resize/rotate/move interaction with snapping,
  prefabs, copy/paste, project create/open/save/validate/export, play-on-a-copy, console log
  (ADR 0006). Unit tested without a window.
- [x] **Editor UI.** Dear ImGui docking shell: hierarchy, reflection-generated inspector, scene view
  with gizmos and overlays, game view, assets, console, dialogs, Play/Pause/Step/Stop (ADR 0007).
- [x] **Automated UI tests.** A scripted driver that pushes real SDL events into the real UI; three
  scripts (full new-project workflow, sample play, sample editing) run under CTest.
- [x] **Packaging and documentation.** Install rules and CPack archives (editor, player, sample
  project, docs, licenses), export-game command, dependency notices, ADRs 0004 to 0007.

## Prototype requirements and where they live

| Requirement | Component or system | Verified by |
|---|---|---|
| Two independently controlled characters | `PlatformerController` with per-entity keys (P1 WASD, P2 arrows in the sample) | `gameplay` two-player test, `prototype` playthrough |
| Gravity, ground detection, jumping | Physics gravity; controller reads current contacts; variable jump, coyote time, jump buffer | `gameplay` controller tests |
| Solid platforms, collision | `Collider` on layers from the project's matrix | `game_runtime`, `gameplay` |
| Hazards, character-specific hazards | `Hazard.affectsTags` + `Killable` | `gameplay`, `prototype` (each element safe in its own pool) |
| Pressure plates, doors | `PressurePlate` / `Lever` (sources) -> `Door` (receiver) | `gameplay`, editor scripts (link plate to door in the UI, then play) |
| Moving platforms | `MovingPlatform`, riders carried, optionally signal-gated | `gameplay` |
| Collectibles, trigger regions | `Collectible`, `TriggerZone` | `gameplay` |
| Level completion | `Goal` + `LevelFlow` | `prototype` playthrough |
| Respawn and reset | `SpawnPoint`, `Checkpoint`, `Killable`; `restart()` | `gameplay`, `prototype` |
| Basic camera | `Camera` (fixed, follow, fit targets, optional bounds) | `game_runtime` camera test |
| Everything placeable and configurable in the editor | one reflection declaration per component | `editor_core`, `editor_*` scripts |

## Rules for what comes next

Choose work from a real consumer requirement (the prototype, or a second prototype scene that
cannot be built yet) and give each item acceptance criteria before starting. whats-next.md lists
the candidates in order. Do not add speculative interfaces or empty subsystem folders; audio,
animation authoring, tilemaps and scripting each need their own scoped milestone with tests. Game
rules stay out of the engine and gameplay libraries.

Every milestone ends the same way: configure/build/CTest for `dev`, and for `release`, `asan` and
`headless` when shared code changed; `format-check`; `git diff --check`; docs updated in the same
change (status.md gets the exact results and limits, an ADR for durable architecture choices).
