# Project Status

Last updated: 2026-09-27, native macOS arm64 root-layout continuation.

## Completed milestone

The engine-only physics build plan is complete. Source, public headers, tests, docs
and license notices use the current repository-root layout; generated output stays
in ignored build/. No game, sandbox, bundled application or run target remains.

- C++20 static yk::engine; CMake/Ninja Debug, Release, headless and sanitizer presets.
- Private SHA-256-pinned Box2D 3.1.1 and optional SDL3 3.2.28.
- SDL-free headless build; dependency output contains only Box2D.
- RAII physics worlds; opaque lifetime-token/64-bit-serial body/shape/joint handles.
- Static/dynamic/kinematic bodies, multiple shapes, automatic mass/inertia.
- Circle, box, capsule, convex polygon and static/kinematic two-sided segments.
- Gravity, damping, fixed rotation, sleeping, activation, forces, torque and impulses.
- Friction, restitution, 64-bit collision categories/masks and signed collision groups.
- Fixed stepping/substeps, bounded catch-up, dropped-time reporting and interpolation.
- Continuous collision and bullet-body configuration.
- Copied contact begin/end/hit and sensor begin/end events across catch-up ticks.
- Current contacts/manifolds and latest-tick sensor overlaps; disabled/destroyed
  participants are immediately excluded from overlap queries.
- Closest ray cast, exact point query and geometry-bounds AABB query.
- Rigid/spring distance joints and revolute limits/motors; normalized reference angles.
- Safe dependent destruction and historical event identity through native generation wrap.
- Actual collider debug outlines with explicit meters-to-render-units conversion.
- Optional SDL ApplicationLayer loop, window, input, camera, renderer and textures.

The continuation repaired missing CMake project/CTest initialization after f20e619,
aligned presets/docs with root paths, removed native Clang handle-copy warnings,
and fixed FrameClock reset and initial minimized-state handling. Six physics
regressions fail against the original implementation and pass with these fixes.

## Current verification

Host: macOS Darwin 25.6.0, arm64; AppleClang 21.0.0 (clang-2100.1.1.101),
CMake 4.4.3, Ninja 1.13.2, clang-format 23.1.1.
This session used existing tools in /private/tmp/yk-engine-tools/bin on PATH.
No machine-wide tool installation was required.

From the repository root, execute all three commands for each preset:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Substitute release, headless or asan. Outputs live in build/<preset>.
Additional verification:

```sh
cmake --build --preset dev --target format-check
cmake -E chdir build/dev ./yk_runtime_tests --native-smoke
git diff --check
```

On Windows the diagnostic executable is yk_runtime_tests.exe in that same directory.
The native diagnostic runs 12 bounded frames, writes native-frame.bmp and exits.
Runtime CTest uses SDL dummy/software drivers and removes transient BMP files.

| Gate | Current result |
|---|---|
| macOS Debug configure/build/CTest | Passed; 3/3 (core, physics, runtime) |
| macOS Release configure/build/CTest | Passed; 3/3 |
| macOS headless configure/build/CTest | Passed; 2/2 (core, physics); no SDL dependency output |
| macOS AddressSanitizer + UBSan configure/build/CTest | Passed; 3/3 |
| clang-format format-check | Passed |
| git diff --check | Passed |
| Native SDL Metal diagnostic | Exit 0; 12 frames; clean shutdown |
| Native Retina readback | 2400x1350 BMP visually inspected; debug outline matches geometry/camera |
| External add_subdirectory consumer | Configure/build/run passed; physics advances with BUILD_TESTING=OFF and YK_RUNTIME=OFF |
| Consumer isolation | Zero CTest tests; no SDL/test build targets or SDL dynamic dependency |

Physics performs 110 behavioral checks covering fixed-frame partitioning,
gravity/mass/forces, disable/re-enable, friction, restitution, sleeping, contact lifecycle,
sensor entry/exit/nonblocking behavior, filtering, spatial queries, geometry validation,
foreign/stale handles, world recreation, springs, hinge motors/limits/reference angles,
high-speed thin-wall CCD, a 12-body stack, 70,000 shape recycling cycles, a separate
65,536-cycle pending sensor-event generation-wrap case, catch-up events and interpolation.

Runtime tests use real software-renderer BMP pixel checks for camera projection,
layer/depth/submission ordering and physics debug meter conversion. They also verify
failure cleanup, texture ownership/caching/release, resize, focus/input/quit transitions,
first-frame/reset timing and minimized/restored input behavior.

ASan/UBSan instruments project code and Box2D; SDL is not instrumented. No separate
leak-checker result is claimed. Project code passes warnings-as-errors; upstream SDL
macOS SDK deprecation warnings are separate from project warnings.

## Historical Windows evidence

The previous status record reports Windows x64 MSVC 19.44.35229/VS 2022 Build Tools:
Debug, Release and AddressSanitizer CTest 3/3; headless 2/2; format/diff checks;
12-frame native Direct3D 11 smoke and visually inspected readback. Those results
precede the root-layout continuation and are retained as historical evidence.
The current changes were not executed on MSVC. MSVC's preset uses ASan without UBSan.

## Limits

- Consumer applications supply their own executable and physics advance calls.
- Current native verification is macOS arm64; current Windows revalidation and Linux
  verification remain future platform work.
- Minimize/restore, focus and key behavior are integration-tested through SDL events;
  hands-on desktop keyboard/focus/minimize/resize behavior was not manually exercised.
- Physics is single-threaded per world. Same-build fixed-tick repeatability is tested;
  cross-platform bitwise determinism and arbitrary speed/scale correctness are not claimed.
- CCD does not guarantee bullet-vs-bullet or swept sensor detection.
- Forces are consumed per solver tick; sustained forces require application each tick.
- No shape casts, controller/one-way-platform policy, custom contact callbacks,
  physics serialization, additional joint families or automatic sprite synchronization.
- No scenes, tilemaps, sprite-sheet animation, PNG loader, controller input, UI or audio.
- Texture metadata is append-only; asset sharing/unload policy remains caller-owned.
- Runtime callers must use the OS main thread; creation-thread assertions alone cannot
  establish that an application's first SDL initialization occurs on the OS main thread.
- No standalone installer or CI pipeline is supplied.

API contracts: physics.md and architecture.md. Durable decisions: decisions/0001,
decisions/0002 and decisions/0003. Audit/remediation history: cleanup-report.md.

## Linux playable-foundation continuation (2026-09-27)

Elemental Escape adds a production-path executable, two validated connected maps, fixed-tick input
and movement, PNG atlas animation, bitmap UI, pause/menu flow, persistent key/door/checkpoint state,
hazards/respawn, event tones, and an installable package. Exact current behavior and remaining
limits (notably moving-platform carry and unverified Windows/macOS packaging) are documented in
`game-foundation.md`. This section supersedes older statements above that no game target exists.
