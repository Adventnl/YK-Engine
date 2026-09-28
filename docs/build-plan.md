# Engine Build Plan

## Goal

Build and maintain the reusable core 2D engine itself, with a usable rigid-body
physics library. This replaces the previous two-game roadmap at the user's request.
Game vertical slices, castle systems, quests, inventory and platformer rules are out of scope.

## Layout

The latest repository reorganization places engine modules at the root:

```text
/
  CMakeLists.txt
  CMakePresets.json
  README.md
  .clang-format
  .gitignore
  include/yk/{core,input,graphics,physics}/
  src/{core,input,platform,graphics,physics}/
  tests/{unit,integration}/
  docs/decisions/
  LICENSES/
  THIRD_PARTY.md
  build/                    # ignored generated output only
```

Create only used modules. There is no games directory or game/run target.

## Engine physics milestone

- [x] Remove sandbox source/targets and replace the Game runtime interface with ApplicationLayer.
- [x] Consolidate source, tests, docs and licenses in the current root layout; keep build output ignored.
- [x] Integrate SHA-256-pinned Box2D privately, with a headless build path.
- [x] Implement world ownership; lifetime-safe bodies/shapes/joints; physical materials and filtering.
- [x] Support circles, boxes, capsules, convex polygons and static/kinematic segments.
- [x] Add fixed stepping, catch-up limits, interpolation, forces/impulses and configurable gravity.
- [x] Add copied contact/impact/sensor events, current contacts/overlaps and spatial queries.
- [x] Add distance/spring constraints and hinges with angular limits/motors.
- [x] Integrate actual shape outlines into the renderer with explicit meters-to-world conversion.
- [x] Verify current Debug/Release/headless/sanitizer builds, physical scenarios and renderer readback.
- [x] Rewrite docs around actual engine capability, current layout and verified boundaries.

This milestone is complete. Completion evidence and remaining limits are tracked in status.md. The next pass
must remain engine-focused; it must not reintroduce a game to demonstrate progress.
The original Windows verification is retained as historical evidence; this continuation
verifies the current root layout on native macOS.

## Subsequent work

Only add reusable engine capabilities when requested. Possible physics-focused
extensions include shape casts, additional joint types, profiling and an explicit
fixed-tick force/controller callback. These are not scaffolded or claimed implemented.

Audio, UI, scene serialization, tilemaps, animation and an editor are separate
engine milestones requiring their own scoped acceptance criteria.

## Playable foundation continuation (2026-09-27)

The repository owner explicitly superseded the earlier engine-only product boundary. The runtime
now builds the Elemental Escape reference game and reusable level/controller/animation/save
foundation. The implemented and remaining scope is recorded in `game-foundation.md`; a second
elemental character, puzzle mechanics, music/settings UI, and
non-Linux package verification are the next coherent work items.
