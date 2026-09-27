# Project Status

Last updated: 2026-09-27, Windows x64 engine-only physics pass.

## Implemented and verified

The engine-only physics milestone is complete on Windows x64. Root project directories
are only engine/ and Git metadata. All source, public headers, tests, docs, license
notices and generated build output are inside engine/. No game/sandbox/run target remains.

- C++20 static yk::engine; CMake/Ninja Debug, Release, headless and sanitizer presets.
- Private SHA-256-pinned Box2D 3.1.1 and optional SDL3 3.2.28.
- SDL-free headless build. Its dependency directory contains only Box2D source/build state.
- RAII physics worlds; opaque lifetime-token/64-bit-serial body/shape/joint handles.
- Static/dynamic/kinematic bodies; multiple shapes per body; automatic mass/inertia.
- Circle, box, capsule, convex polygon and static/kinematic two-sided segment geometry.
- Gravity, damping, fixed rotation, sleeping, activation, forces, torque and impulses.
- Friction, restitution, 64-bit collision categories/masks and signed collision groups.
- Fixed stepping/substeps, bounded catch-up, dropped-time reporting and pose interpolation.
- Continuous collision and bullet-body configuration.
- Copied contact begin/end/hit and sensor begin/end events across all catch-up ticks.
- Current contacts/manifolds and sensor-overlap queries.
- Closest ray cast, exact point query and geometry-bounds AABB query.
- Rigid/spring distance joints and revolute limits/motors; safe dependent destruction.
- Actual collider debug outlines and explicit meters-to-render-units conversion.
- Existing SDL application/window/renderer/input/camera/texture capabilities retained.
  Generic ApplicationLayer replaces Game; no bundled application entry point.

## Toolchain and exact commands

Verified host: Windows x64, MSVC 19.44.35229, Visual Studio 2022 Build Tools.
CMake/Ninja/clang-format came from the installed Visual Studio tooling.
Use an x64 Native Tools Command Prompt with CMake/Ninja on PATH, from the repo root.

For each preset dev, release, headless, asan:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Substitute the preset name in all three commands. Outputs live in engine/build/<preset>.
Additional verification:

```sh
cmake --build --preset dev --target format-check
cmake -E chdir engine/build/dev engine/yk_runtime_tests.exe --native-smoke
git diff --check
```

The native smoke mode is an engine integration diagnostic, not a game target. It runs
12 bounded frames, writes native-frame.bmp in the chosen build directory and exits.
Runtime CTest uses SDL dummy/software drivers and removes its transient BMP files.

## Results

| Gate | Result |
|---|---|
| Windows MSVC Debug configure/build/CTest | Passed; 3/3 (core, physics, runtime) |
| Windows MSVC Release configure/build/CTest | Passed; 3/3 |
| Windows MSVC headless configure/build/CTest | Passed; 2/2 (core, physics) |
| Windows MSVC AddressSanitizer configure/build/CTest | Passed; 3/3 |
| clang-format format-check | Passed |
| git diff --check | Passed |
| Native SDL Direct3D 11 diagnostic | Exit 0; 12 frames; clean shutdown |
| Native frame readback | Visually inspected; physics outline aligns with sprite/camera |

The physics executable performs 104 behavioral checks, including fixed-frame partitioning,
gravity/mass/forces, disable/re-enable, friction, restitution, sleeping, contact lifecycle,
sensor enter/exit/nonblocking behavior, filtering, ray/point/AABB queries, geometry
validation, foreign/stale handles, world recreation, spring convergence, motor/limit
behavior, a thin-wall high-speed CCD case, a 12-body stack, 70,000 shape create/destroy
cycles across native generation wrap, catch-up event retention and interpolation bounds.

Runtime tests retain SDL failure cleanup, input/focus/quit transitions, renderer/texture
lifetime, canonical BMP caching, resizing, camera/ordering pixel checks, and now verify
physics debug pixels against actual meter conversion and camera projection.

AddressSanitizer instruments project code **and Box2D**. SDL is not instrumented.
MSVC has no UBSan in this configuration; Clang/GCC use ASan + UBSan, but those paths
were not executed on this Windows host. No leak-checker result is claimed.
Project builds pass warnings-as-errors. Upstream warnings are handled separately.

## Limits and platform status

- This is the core engine library. Consumer applications supply their own executable and loop.
- New physics work is verified on Windows x64; no new macOS/Linux build is claimed.
- Native Direct3D rendering/readback and bounded launch verified; hands-on keyboard,
  focus switching, minimize/restore and desktop resize were not manually exercised.
- Physics is single-threaded per world. Same-build fixed-tick repeatability is tested;
  cross-platform bitwise determinism and arbitrary-speed/scale behavior are not claimed.
- Continuous collision does not guarantee bullet-vs-bullet or swept sensor detection.
- Forces are consumed per solver tick; sustained forces must be applied each fixed tick.
- No shape casts, one-way-platform/controller policy, custom contact callbacks,
  physics serialization, additional joint families or automatic sprite synchronization.
- No scenes, tilemaps, sprite-sheet animation, PNG loader, controller input, UI or audio.
- Renderer texture metadata remains append-only; asset sharing/unload policy is still caller-owned.
- No standalone package installer or CI pipeline is supplied.

API contracts: physics.md and architecture.md. Durable decisions: decisions/0001 and
decisions/0002. The completed audit/remediation record is cleanup-report.md.
