# YK Engine

A proprietary C++20 **2D engine library** with a working rigid-body physics subsystem.
This repository contains the engine itself. All headers, implementation, tests,
documentation and license notices live in this repository; generated builds live in `build/`.

Physics runs independently of SDL. It provides static/dynamic/kinematic bodies,
circles, boxes, capsules, convex polygons, static/kinematic segments, collision
filters, sensors, friction/restitution, sleeping, continuous collision, forces,
impulses, distance/spring joints, motorized hinges, fixed stepping, contact events,
ray/point/AABB queries, interpolation and debug outlines.

Optional SDL3 support provides the application loop, input, sprite renderer and
camera. There is no bundled game, sandbox or game executable.

Requires CMake 3.25+, Ninja and a C++20 compiler. On Windows use Visual Studio 2022
Build Tools with the C++ workload in an **x64 Native Tools Command Prompt**.
The first configure downloads SHA-256-pinned Box2D 3.1.1 and SDL3 3.2.28.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

For physics without SDL, use the `headless` preset with the same configure/build/test
commands. `release` and `asan` presets are also provided. The sanitizer preset uses
AddressSanitizer with MSVC, or AddressSanitizer + UBSan with Clang/GCC.
If clang-format is installed, `format` and `format-check` targets are available.

Consumers use `add_subdirectory(path/to/YK-Engine)` and link `yk::engine`.
Set `BUILD_TESTING=OFF` for a library-only build. Set `YK_RUNTIME=OFF` to avoid
downloading/linking SDL. Public physics headers contain no Box2D or SDL types.

Read [physics usage](docs/physics.md), [architecture](docs/architecture.md),
[verified status](docs/status.md), [build plan](docs/build-plan.md),
and [dependency notices](THIRD_PARTY.md).

## Playable reference game

Runtime builds now include **Elemental Escape**, a packaged two-area platforming foundation with
validated tilemap levels, persistent keys/doors/checkpoints, fixed-tick character movement,
PNG sprite animation, UI, event audio, pause, hazards, and save/continue. Build and run it with:

```sh
cmake --preset dev && cmake --build --preset dev
./build/dev/elemental_escape
```

See [the game foundation guide](docs/game-foundation.md) for controls, level authoring, packaging,
architecture, verified platforms, and current limitations.
The build requires Python 3 to decode the repository-safe textual PNG payload; packaged builds do
not require Python.
