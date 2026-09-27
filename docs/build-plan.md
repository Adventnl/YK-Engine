# Engine Build Plan

## Goal

Build and maintain the reusable core 2D engine itself, with a usable rigid-body
physics library. This replaces the previous two-game roadmap at the user's request.
Game vertical slices, castle systems, quests, inventory and platformer rules are out of scope.

## Layout

```text
/
  CMakeLists.txt
  CMakePresets.json
  README.md
  .clang-format
  .gitignore
  engine/
    CMakeLists.txt
    include/yk/{core,input,graphics,physics}/
    src/{core,input,platform,graphics,physics}/
    tests/{unit,integration}/
    docs/decisions/
    LICENSES/
    THIRD_PARTY.md
    build/                    # ignored generated output only
```

Create only used modules. There is no games directory or game/run target.

## Current implementation pass

- Remove sandbox source/targets and replace the Game runtime interface with ApplicationLayer.
- Consolidate tests, docs, licenses and build output into engine/.
- Integrate SHA-256-pinned Box2D privately, with a headless build path.
- Implement world ownership; lifetime-safe bodies/shapes/joints; physical materials and filtering.
- Support circles, boxes, capsules, convex polygons and static/kinematic segments.
- Add fixed stepping, catch-up limits, interpolation, forces/impulses and configurable gravity.
- Add copied contact/impact/sensor events, current contacts/overlaps and spatial queries.
- Add distance/spring constraints and hinges with angular limits/motors.
- Integrate actual shape outlines into the existing renderer with explicit meters-to-world conversion.
- Verify Windows Debug/Release/headless/sanitizers, physical scenarios and renderer readback.
- Rewrite docs around actual engine capability and boundaries.

Completion evidence and remaining limits are tracked in status.md. The next pass
must remain engine-focused; it must not reintroduce a game to demonstrate progress.

## Subsequent work

Only add reusable engine capabilities when requested. Possible physics-focused
extensions include shape casts, additional joint types, profiling and an explicit
fixed-tick force/controller callback. These are not scaffolded or claimed implemented.

Audio, UI, scene serialization, tilemaps, animation and an editor are separate
engine milestones requiring their own scoped acceptance criteria.
