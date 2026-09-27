# Project Status

Last updated: 2026-09-27 (native macOS implementation pass).

## Milestones and repository reality

- **M0 Repository Bootstrap: COMPLETE** on the available macOS host.
- **M1 Runtime + Rendering: IN PROGRESS**. The implementation is integrated and tested;
  hands-on desktop movement/focus/resize verification remains pending.
- M2–M8: NOT STARTED.

The repository originally contained only README and planning docs. It now builds
`yk_engine`, `yk_sandbox`, `yk_unit_tests` and `yk_runtime_tests` through CMake.

## Implemented

- C++20 static engine; CMake/Ninja Debug, Release and sanitizer presets.
- Project warnings treated as errors: Clang/GCC `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`,
  MSVC `/W4 /WX /permissive-`. Third-party warnings are separate.
- clang-format configuration and optional `format`/`format-check` targets.
- RAII SDL lifetime, resizable/high-DPI window and native renderer, with partial-failure cleanup.
- Single-use synchronous Application/Game boundary; no global engine services.
- Ordered input/update/clear/submit/present loop, quit handling, structured stderr logging,
  Debug assertions and contextual Result errors.
- Monotonic seconds-based delta; first frame zero, max 100 ms, zero simulation delta when unfocused/minimized.
- Keyboard held/pressed/released, repeat suppression, short taps, focus-loss release and game-owned multi-key bindings.
- Renderer-owned textures with non-owning, renderer-specific validated handles and explicit release.
- RGBA upload, canonical absolute-path BMP loading/cache; no file required for the demo.
- Sprite position, scale, rotation, anchor, tint and explicit world dimensions.
- Queue order by layer/depth/submission sequence; world debug rectangles.
- Orthographic camera translation/zoom and inverse coordinate mapping.
- Fixed logical viewport with physical-pixel scaling and letterboxing on resize/Retina.
- Optional bounded-run BMP readback before present; captures exclude letterbox bars.
- Sandbox checker sprite, normalized WASD/arrows movement at 240 units/s,
  Q/E camera pan, Space reset, Escape/window-close exit.

## Dependencies and tools

SDL3 **3.2.28**, statically built via CMake FetchContent from the official release archive.
SDL revision: `SDL-release-3.2.28-0-g7f3ae3d57`.
SHA-256: `1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211`.
No floating dependency revision or system SDL fallback. The first configure downloads
and verifies source; subsequent runs reuse the build-tree copy. An initial local source
override was used to inspect headers, then removed; canonical FetchContent downloads
were verified for dev/release/asan. The build does not depend on that temporary override.

`THIRD_PARTY.md` and `LICENSES/` record SDL and bundled component notices. SDL tests,
examples and install rules are disabled; external HIDAPI libusb is disabled. Native
binary linkage was inspected and contains OS frameworks/libraries, not Homebrew dependencies.

Host tools used: Apple Clang 21.0.0, CMake 4.4.3, Ninja 1.13.2, clang-format 23.1.1.
CMake/Ninja/clang-format were installed into `/private/tmp/yk-engine-tools`, not globally.
Minimum CMake version is 3.25. No clang-tidy target is configured; compiler warnings,
format checks and sanitizer presets are the current useful checks.

## Canonical commands that worked

Run from the repository root. On this host, first add temporary tooling to PATH:

```sh
export PATH=/private/tmp/yk-engine-tools/bin:$PATH
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target run
```

`run` is interactive; Escape/window close stops it. The run target was launched with
Metal and stopped cleanly via SDL's SIGTERM-to-quit handling, with exit code zero.
The temporary tools directory may not survive a reboot. If missing, install tools
normally, or recreate the isolated environment:

```sh
python3 -m venv /private/tmp/yk-engine-tools
/private/tmp/yk-engine-tools/bin/pip install cmake==4.4.3 ninja==1.13.2 clang-format==23.1.1
export PATH=/private/tmp/yk-engine-tools/bin:$PATH
```

Additional verified gates, run sequentially:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cmake --preset asan
cmake --build --preset asan
ctest --preset asan --output-on-failure
ctest --preset dev -R runtime --repeat until-fail:10 --output-on-failure
```

Native bounded launches verified with exit code zero and clean shutdown logs:

```sh
build/dev/yk_sandbox.app/Contents/MacOS/yk_sandbox --frames 12 --capture /private/tmp/yk-final-native-frame.bmp
build/release/yk_sandbox.app/Contents/MacOS/yk_sandbox --frames 12
build/asan/yk_sandbox.app/Contents/MacOS/yk_sandbox --frames 12
```

On Windows the executable is `build/dev/yk_sandbox.exe`; use the same CLI options.
This Windows path/configuration is established by CMake but has **not** been run here.
Use an x64 MSVC developer prompt plus Ninja; 32-bit Windows configurations are rejected.

## Verification results

- Clean configure/download/build succeeded for all three presets.
- Final dev/release/asan CTest runs each passed **3/3**: core, runtime and sandbox_smoke.
- Format check passed; project builds have no warnings. SDL 3.2.28 itself emits Cocoa
  deprecation warnings with the current macOS SDK; these are not promoted to project errors.
- Core tests exercise keyboard/action edges, alias handover, invalid bindings, delta bounds,
  first frame, diagonal normalization and camera coordinate round trips.
- Runtime tests exercise actual SDL event polling with injected key/focus/quit events,
  focus-loss release and zero delta, short taps, invalid handles/configuration,
  SDL/window/renderer/game failure cleanup, texture release/cache/recreation,
  real window resizing and software pixel readback. Pixels prove texture colors,
  translated/zoomed camera, dimensions, layer/depth/tie order and clear color.
- Actual sandbox runs for eight frames in CTest with SDL dummy/software drivers.
- Ten serial runtime repetitions passed; ten more repetitions per preset passed concurrently
  (30 concurrent-run repetitions). One earlier three-preset concurrent run timed out
  after logging initial cleanup. No code cause was found and immediate reruns did not
  reproduce it. Do not silently treat a recurrence as success; investigate if it returns.
- Native Cocoa/Metal launches passed in Debug/Release/sanitizer builds. A 1920x1080
  Retina frame readback was visually inspected and showed the checker sprite centered
  inside the projected world outline. No AddressSanitizer/UBSan diagnostics appeared.
- SIGTERM cleanly stops the native interactive loop. No leak detector result is claimed;
  SDL itself is not sanitizer-instrumented and OS/framework allocation leaks are not measured.
- `git diff --check` passed.

## Platform and manual-verification limits

macOS arm64: native compilation, Metal rendering/readback, bounded execution, signal
shutdown and automated SDL/software scenarios verified.
Windows x64: intended primary target; portable CMake/MSVC settings implemented, but
no Windows build/runtime verification was possible on this host.

Computer Use refused the new sandbox bundle with “Computer Use was not approved to
use YK Sandbox.” No desktop window controls or physical keyboard input could be
exercised through the available tool. Therefore hands-on WASD/arrows, focus switching,
minimize/restore, interactive resize and close-button verification are still pending.
Native frame readback and automated event/resize tests do not substitute for those checks.

## Current limitations and decisions

- Main-thread, one-window, one-Application runtime; no externally owned SDL embedding.
- Finite positive sprite scale only, whole-texture sprites only; no flips, atlas/source regions,
  PNG loading, animation, tilemap, scenes, collision, controller input, UI or audio yet.
- Explicit depth is supported; automatic Y-sort policy is not implemented.
- No fixed-step simulation. Focus/minimize pauses delta, but callbacks/rendering still run.
- Texture slots are not recycled; short-lived texture churn grows metadata until renderer teardown.
  Releasing a cached texture invalidates every alias; there is no reference-counted scene asset policy.
- Diagnostic capture happens only on the requested final bounded frame; early exit can produce no capture.
- macOS bundle is a development app, not signed/notarized/distribution packaging.
- No CI or packaging pipeline yet.

Architecture contracts are in `docs/architecture.md`; durable backend/dependency/resource
choices are in `docs/decisions/0001-sdl-renderer-and-resource-lifetime.md`.
Key locations: `engine/include/yk`, `engine/src/platform/Application.cpp`,
`engine/src/graphics/Renderer.cpp`, `games/sandbox/src/main.cpp`, `tests/unit`,
`tests/integration`, root CMake/presets and `LICENSES/`.
