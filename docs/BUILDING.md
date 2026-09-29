# Building, testing and shipping

## What you need

- CMake 3.25 or newer, Ninja, and a C++20 compiler: GCC 13 or Clang on Linux and macOS, MinGW-w64
  or MSVC (x64) on Windows.
- On Linux the usual SDL development packages (X11 or Wayland, ALSA or PulseAudio); the first
  configure prints what SDL could not find. Nothing else is installed system-wide: Box2D, SDL3,
  Dear ImGui and stb_image are built from pinned sources.
- Python 3 with Pillow and numpy only to regenerate the demo game's art and levels
  (`YK-DemoGame/tools/`); the demo's generated files are checked in, so building and testing need
  no Python.

The first configure downloads Box2D 3.1.1 and SDL 3.2.28 (SHA-256 checked) and Dear ImGui 1.92.9
(by commit). On a network that cannot fetch them run `scripts/fetch-deps.sh` once and point the
build at the result:

```sh
scripts/fetch-deps.sh /path/to/deps          # verified shallow checkouts
export YK_DEPS_DIR=/path/to/deps             # or -DYK_DEPS_DIR=... on the cmake command line
```

## Presets

```sh
cmake --preset dev                            # Debug, tests on: build/dev
cmake --build --preset dev
ctest --preset dev                            # 27 tests, about two minutes
```

| Preset | What it is |
|---|---|
| `dev` | Debug build with warnings as errors and tests. |
| `release` | Optimized build, same tests. This is what `scripts/package.sh` packs. |
| `asan` | Address and undefined-behavior sanitizers on project code. |
| `headless` | `YK_RUNTIME=OFF`: no SDL and no window. The engine, the gameplay library, the editor core, the command line and their tests still build and run. |
| `windows-cross` | 64-bit Windows from Linux with MinGW-w64 (see [Windows](#windows)). |

Options: `YK_RUNTIME` (default ON) builds the window, renderer, audio, player and editor;
`YK_SANITIZERS` adds the sanitizers; `BUILD_TESTING=OFF` gives a library-only build.

Programs, in the build folder:

| Program | What it does |
|---|---|
| `yk_editor` | The editor ([EDITOR.md](EDITOR.md)). |
| `yk_player` | Runs a project as a game: `yk_player YK-DemoGame`. `--fixed --frames 120 --capture shot.bmp` runs deterministically for a while and saves the last frame. |
| `yk` | Command line: `yk validate`, `yk format [--check]`, `yk info`, `yk components`, `yk export`, `yk targets`. Headless. |

```sh
./build/dev/yk_editor                        # the welcome screen offers the demo game
./build/dev/yk_player YK-DemoGame            # play it: Ember A/D/W/S, Tide arrows
./build/dev/yk validate YK-DemoGame
```

## Tests

`ctest --preset <name>` runs everything:

- **Unit and integration suites** for every module: core, JSON, physics, scene and reflection,
  input, animation, assets, effects, audio, the runtime, the gameplay library, the export
  pipeline, the editor core (documents, selection, gizmos, projects, prefabs, layout), the
  renderer and audio backends (software renderer and dummy audio, no window needed).
- **`demo`** plays the demo game's level with a bot that presses the real actions through the
  real runtime, from the first step to the last exit, and fails when the level cannot be
  completed. It also checks the generated component reference (`docs/components.md`) and that
  each character dies only in the hazard that is deadly to it.
- **`editor_*`** drive the real editor UI by injected mouse and keyboard events under SDL's dummy
  video driver: a complete workflow from an empty project to an exported game
  (`editor_workflow`), the demo level being edited, played, restyled and exported
  (`editor_demo_edit`, `editor_demo_play`, `editor_demo_settings`, `editor_demo_assets`,
  `editor_demo_workbench`) and the layout surviving a restart (`editor_layout_*`). Screenshots of
  failures are saved next to the test's working folder (`build/<preset>/editor-tests/<name>/shots`).
- **`install`** installs the build into a scratch folder, packs it with CPack and checks the
  contents, the absence of the private dependencies' headers, and that the installed programs
  work where they were put (the installed editor opens and plays the installed sample; the
  installed `yk` exports it).

Two scripts run the whole matrix:

```sh
scripts/verify.sh                 # format check, git diff --check, then every preset's build + tests
scripts/verify-windows.sh         # cross-compile for Windows, run all tests under Wine, export + run the demo
```

Formatting is enforced by clang-format (`cmake --build --preset dev --target format`, and
`format-check` in CI-style runs). Warnings are errors: `-Wall -Wextra -Wpedantic -Wconversion
-Wshadow -Werror` (`/W4 /WX` on MSVC).

## Installing and packaging

```sh
cmake --install build/release --prefix /opt/yk-engine    # bin/, share/yk-engine/, share/doc/
scripts/package.sh                                        # build/package/YKEngine-<version>-<system>.tar.gz (.zip on Windows)
```

An installation contains `yk_editor`, `yk_player`, `yk`, the demo game
(`share/yk-engine/YK-DemoGame`), the documentation and the license notices
(`share/doc/YKEngine/licenses`). It contains none of the private dependencies' headers or
libraries; the `install` test checks that.

## Exporting a game

An export is the player program for a target system, renamed after the game, next to a copy of the
project's data (layouts in [PROJECT_FORMAT.md](PROJECT_FORMAT.md#exported-games)).

```sh
yk export MyGame --target windows --out dist --zip
yk export MyGame --target macos --player path/to/macos/yk_player --out dist
yk targets                                   # which systems have a player program from here
```

Or use **Build > Export Game** in the editor. Either way:

- The project is validated first, and the exported copy is validated on its own afterwards; an
  export that would be missing a file it needs fails and leaves nothing behind.
- The player must be built **for the target**. Exporting for the system you are on uses the
  `yk_player` next to the editor or `yk`. For another system pass `--player <file>`, put the
  player in `templates/<windows|macos|linux>/` next to the editor, in
  `<prefix>/share/yk-engine/templates/<target>/`, or in `$YK_TEMPLATES/<target>/`.
- The player carries SDL3, Box2D and stb_image, whose licenses require their notices to
  travel with it; the export copies them into `licenses/` when it can find the folder (an
  installation or a build tree) and warns when it cannot.
- Nothing is compressed or encrypted: the data folder is the project's files. `--zip` writes a
  reproducible archive (fixed timestamps, executable bit kept) beside the folder.

## Windows

**Native.** Configure with the `dev` or `release` preset from a Visual Studio x64 Developer
prompt (or with MinGW-w64), or use CMake's Visual Studio generator; the project builds with
`/W4 /WX`. This path is **not verified in this repository's environment**: the Windows checks
below were done with MinGW-w64 under Wine, not with MSVC on Windows.

**From Linux (verified).** With `mingw-w64` (posix threads) and `wine64` installed:

```sh
cmake --preset windows-cross && cmake --build --preset windows-cross
scripts/verify-windows.sh
```

The toolchain file is `cmake/toolchains/mingw-w64-x86_64.cmake`. The result has no runtime DLLs
(the C++ runtime, the pthread shim and SDL are linked statically). The script runs every test
executable under Wine, including the whole demo playthrough (it reaches the same simulated result
as on Linux), exports the demo for Windows with the Windows `yk.exe` and starts the exported game
for a few seconds, saving a frame. The editor's UI scripts are not run under Wine.

Not done on Windows: an application icon and version resource for the executables, code signing.

## macOS

The build is the same CMake project; use `dev` or `release` on a Mac with Xcode's command line
tools. An export for macOS is an application bundle (`<Product>.app`) with an `Info.plist` and the
project's data in `Contents/Resources/data`, which is where SDL reports the base path for a
bundle. **This is implemented and covered by unit tests of the bundle layout, but it has not been
run on a Mac**, because the environment this was developed in has none, and cross-compiling the
player needs Apple's SDK. Not done: an `.icns` icon, code signing and notarization; an unsigned
bundle needs to be opened once through the context menu on a current macOS.

## Game modules (custom C++ components)

The stock editor, player and command line know the engine's components and the gameplay library.
There is no scripting language: a behavior the gameplay library does not have is a C++ component.
A game that needs one puts its components in a library and builds its own copies of the three
programs with those components registered. The copies take the same options, read and write the
same files and export the same way.

1. Write the components (a class with public fields and a `describe` function, see
   [ARCHITECTURE.md](ARCHITECTURE.md#scenes-components-and-reflection-scene-components); a complete
   example is `tests/game_module/SpinnerModule.*`) and a function that registers them:
   `void registerComponents(yk::ComponentRegistry &)`.
2. In your project's `CMakeLists.txt` add the engine as a subdirectory (or with `FetchContent`) and
   ask for the programs:

```cmake
add_subdirectory(YK-Engine)
add_library(my_game_module STATIC src/MyGame.cpp)
target_link_libraries(my_game_module PUBLIC yk::gameplay)
yk_add_game_hosts(my_game MODULE my_game_module HEADER MyGame.hpp
    REGISTER my_game::registerComponents)
```

   This creates `my_game_player`, `my_game_editor` and `my_game_tool` (the `yk` command line).
   Each is one generated `main` that calls `yk::host::runPlayer`, `runEditor` or `runTool`
   (`include/yk/host/Hosts.hpp`) with your function, so you can also write those mains yourself.
3. Edit with `my_game_editor`, run with `my_game_player`, check with `my_game_tool validate`. The
   stock programs refuse such a project ("unknown component"); the test `game_module_tool_stock`
   pins that.
4. Export with the game's own player, because the exporter copies whichever player it is given:
   `my_game_tool export MyGame --target linux --player path/to/my_game_player --out dist`, or the
   Export dialog of `my_game_editor`, whose *Player program* field takes the path.

There is no dynamic loading: a game's editor is built with its components, so the components'
reflection, validation and export behave exactly as in the stock tools, and there is no ABI to
keep stable (ADR 0011). The repository builds and runs an example module in its own test suite
(`game_module`, `game_module_tool`, `editor_game_module`).

## Troubleshooting

- *"The player program was not found"* when exporting: build `yk_player` (it is part of the
  default build) or pass `--player`.
- *A UI test fails in the editor and you need to see why*: open
  `build/<preset>/editor-tests/<name>/shots/failure-line-<n>.bmp`; the console output of the run
  names the expectation.
- *SDL cannot open a display in CI*: the tests set `SDL_VIDEODRIVER=dummy` and
  `SDL_RENDER_DRIVER=software` themselves; set the same for your own runs.
- *Behind a proxy*: `scripts/fetch-deps.sh` and `YK_DEPS_DIR` (above).
