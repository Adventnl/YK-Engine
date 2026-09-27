# YK Engine

A proprietary C++20, 2D-first engine above SDL3. Windows x64 is the primary target;
macOS builds natively from the same tree. The current sandbox renders a procedural
checker sprite through the real application loop.

Requires CMake 3.25+, Ninja, a C++20 compiler and internet access for the first SDL3
source fetch. macOS requires Xcode Command Line Tools. On Windows, use an **x64
Native Tools Command Prompt** with Visual Studio 2022's C++ workload and Ninja.
Windows builds have not yet been verified on Windows hardware.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target run
```

WASD or arrows move the sprite; Q/E pan the camera; Space resets; Escape or closing
the window exits. Movement is 240 world units/second with normalized diagonals.
The logical 960x540 viewport scales with letterboxing when resized, including on Retina displays.

For a bounded native launch and diagnostic frame capture on macOS:

```sh
build/dev/yk_sandbox.app/Contents/MacOS/yk_sandbox --frames 12 --capture build/dev/frame.bmp
```

On Windows, use `build/dev/yk_sandbox.exe` with the same arguments. Captures contain
the physical content viewport, excluding letterbox bars. Headless CTest uses SDL's
real dummy video/software rendering backend; it does not validate desktop interaction.

`release` and `asan` configure/build/test presets are also available. `asan` requires
Clang or GCC and instruments project code with AddressSanitizer and UBSan. If
clang-format is found at configure time, `format` and `format-check` targets are enabled.

Read [status](docs/status.md) for verified commands and limitations,
[architecture](docs/architecture.md) for API/lifetime contracts, and
[what's next](docs/whats-next.md) before continuing development.
Dependency versions and license notices are in [THIRD_PARTY.md](THIRD_PARTY.md).
