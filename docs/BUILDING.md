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
ctest --preset dev                            # 42 tests, about twelve minutes on four cores (Debug; CI takes about 17)
```

| Preset | What it is |
|---|---|
| `dev` | Debug build with warnings as errors and tests. |
| `release` | Optimized build, same tests. This is what `scripts/package.sh` packs. |
| `asan` | Address and undefined-behavior sanitizers on project code. |
| `headless` | `YK_RUNTIME=OFF`: no SDL and no window. The engine, the gameplay library, the editor core, the command line and their tests still build and run. |
| `clang` | Clang with libc++ (see [Tests](#tests)). |
| `windows-cross` | 64-bit Windows from Linux with MinGW-w64 (see [Windows](#windows)). |

Options: `YK_RUNTIME` (default ON) builds the window, renderer, audio, player and editor;
`YK_SANITIZERS` adds the sanitizers; `YK_WARNINGS_AS_ERRORS` (default ON except with MSVC);
`BUILD_TESTING=OFF` gives a library-only build.

Programs, in the build folder:

| Program | What it does |
|---|---|
| `yk_editor` | The editor ([EDITOR.md](EDITOR.md)). |
| `yk_player` | Runs a project as a game: `yk_player YK-DemoGame`. `--fixed --frames 120 --capture shot.bmp` runs deterministically for a while and saves the last frame. |
| `yk` | Command line: `yk new`, `yk validate`, `yk format [--check]`, `yk info`, `yk components`, `yk export`, `yk targets`. Headless. |

```sh
./build/dev/yk_editor                        # the welcome screen offers the demo game
./build/dev/yk_player YK-DemoGame            # play it: Ember A/D/W/S, Tide arrows
./build/dev/yk validate YK-DemoGame
./build/dev/yk new ~/Games/MyGame --name "My Game"    # a new project anywhere; edit it with yk_editor
```

Settings that are read at build or run time (all optional):

| Setting | Where | What it does |
|---|---|---|
| `YK_DEPS_DIR` | environment or `-D` | Pre-fetched dependencies (above). |
| `YK_DEMO_PROJECT` | `-D` | The project the tests, the installation and the macOS app use as the sample (default `YK-DemoGame/`). Point it at a checkout anywhere; with none, the tests that need a game are left out. |
| `YK_LOG_DIR` | environment, run time | Replaces the folder logs and crash reports are written to. |
| `YK_NO_DIALOGS` | environment, run time | Reports fatal start-up errors on stderr and in the log only (no message box); the tests set it. |
| `YK_CODESIGN_IDENTITY`, `YK_NOTARY_PROFILE` | environment | Signing and notarization of the macOS release ([macOS](#macos)). |
| `--ui-scale <factor>`, `--debug-crash <kind>` | `yk_editor`, `yk_player` (`--debug-crash`) | Larger interface; crash on purpose to see the report. |

## Tests

`ctest --preset <name>` runs everything:

- **Unit and integration suites** for every module: core, JSON, physics, scene and reflection,
  input, animation, assets, effects, audio, the runtime, the gameplay library, the export
  pipeline, the editor core (documents, selection, gizmos and pins, the view's zoom, projects,
  prefabs, layout), the renderer and audio backends (software renderer and dummy audio, no window
  needed). Two suites play the mechanisms and the level rules on real physics: **`mechanisms`**
  (plates that sink under a character, a crate or a stack, rotating and tilting platforms that carry
  their riders, hinged doors and seesaws, crush protection, collision enter and exit, the frame rate
  not changing the result, the validation messages) and **`level_flow`** (intro, completion
  sequence, failure and retry, the fade between scenes and the session that follows a scene change,
  variables carried over, event reactions and timers). `gameplay` also saves and loads every field
  of every registered component with non-default values and loads a scene written before the newer
  fields existed.
- **`stress`** runs a 41,500-entity scene (40,000 sprites, 1,500 falling bodies) through the real
  runtime and renderer: nothing may be lost, nearly everything off screen must be culled, and the
  time budgets are loose enough to catch only a cost that grows with the square of the scene.
- **`game_module*`** build a small example game module (`tests/game_module`) with
  `yk_add_game_hosts` and check its own command line, player, export and editor (see
  [Game modules](#game-modules-custom-c-components)).
- **`demo`** plays the demo game's level with a bot that presses the real actions through the
  real runtime, from the first step to the last exit, and fails when the level cannot be
  completed. It also checks the generated component reference (`docs/components.md`) and that
  each character dies only in the hazard that is deadly to it.
- **`diagnostics`** covers paths, rotated log files, the running marker and crash reports;
  **`diagnostics_crash`** runs the real editor and crashes it on purpose, checks the report and that
  the next start says the session ended badly (not on Windows: the crash reporter is POSIX-only).
- **`macos_bundle`** assembles `YK Engine.app` from the built programs in a folder the program was
  never built in (a name with a space), and uses it: `yk` finds the player in `Contents/MacOS`, an
  export gets the macOS layout, the exported bundle's program runs from a third folder, and the
  editor finds the sample in `Contents/Resources`. It runs on every system.
- **`external_project`** creates a project and copies the demo into a temporary folder (with a space
  and an umlaut) far from the source tree, then validates, plays and exports them from other working
  directories and runs the export from yet another, checking that its log went to the log folder.
- **`editor_*`** drive the real editor UI by injected mouse and keyboard events under SDL's dummy
  video driver: a complete workflow from an empty project to an exported game
  (`editor_workflow`), the demo level being edited, played, restyled and exported
  (`editor_demo_edit`, `editor_demo_play`, `editor_demo_settings`, `editor_demo_assets`,
  `editor_demo_workbench`, `editor_demo_view`: zoom, fit, focus, panning, pins, the right-click
  menu, `editor_demo_player`: the tracked standalone player), the layout surviving a restart (`editor_layout_*`) and a start-up
  project that is not there (`editor_bad_project`). Screenshots of
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
`format-check` in CI-style runs). Warnings are errors with GCC and Clang (`-Wall -Wextra -Wpedantic
-Wconversion -Wshadow -Werror`; Clang also counts sign conversions). MSVC builds with `/W4` and
reports warnings without failing (CI builds with MSVC on every push; `-DYK_WARNINGS_AS_ERRORS=ON`
enforces `/WX` once a run is confirmed free of them).

The `clang` preset builds everything with Clang and libc++ (what a Mac uses), a stricter standard
library than GCC's: it has caught missing includes and sign conversions before they reached a Mac.

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
yk export MyGame --target macos --dmg --out dist   # on a Mac: MyGame.app signed ad hoc, plus MyGame.dmg
yk export MyGame --target macos --sign "Developer ID Application: Name (TEAMID)" --dmg --out dist
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
- The project's `build.icon` (a square PNG, 512 px or more) becomes `AppIcon.icns` in a macOS bundle
  and the window icon elsewhere; `productName`, `version`, `identifier` and `copyright` go into
  `Info.plist`.
- `--sign [identity]` and `--dmg` need a Mac (`codesign`, `hdiutil`) and fail with that reason
  elsewhere. On a Mac an export is signed ad hoc unless `--no-sign` is given, so that the bundle
  as a whole has a valid seal (Apple silicon requires signed code); ad hoc means "runs on this Mac;
  elsewhere Control-click > Open once". A Developer ID identity signs with the hardened runtime
  and a secure timestamp, which is what notarization requires. The export README says which case
  applies.
- Notarization (the step that removes the warning on other people's Macs) needs your Apple ID
  credentials and is not part of `yk export`. On the exported `.dmg`:
  `xcrun notarytool submit Game.dmg --keychain-profile <profile> --wait` then
  `xcrun stapler staple Game.dmg` (the profile is made once with
  `xcrun notarytool store-credentials`).

## Windows

**For users.** Download the `windows-x64.exe` installer from
[GitHub Releases](https://github.com/Adventnl/YK-Engine/releases) and follow the setup wizard.
The editor appears in the Start menu, and Apps & features can uninstall it. The installer
contains the engine and samples; SDL, CMake, Ninja and Visual Studio are build-time tools
and are not needed to run it. The release workflow builds with the MSVC runtime linked
statically and verifies the installed sample on a Windows runner.

**Native.** Configure with the `dev` or `release` preset from a Visual Studio x64 Developer
prompt (or with MinGW-w64), or use CMake's Visual Studio generator; the project builds with
`/W4`. The CI workflow builds and tests it with MSVC on a real Windows runner
(see [STATUS.md](STATUS.md) for what passes there); the checks below were done with MinGW-w64
under Wine.

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

Not done on Windows: a version resource for the executables, an icon for exported games, and code signing.

## macOS

**For users.** Download the `.pkg` from
[GitHub Releases](https://github.com/Adventnl/YK-Engine/releases) and follow the macOS
Installer wizard, or download the `.dmg` and drag the app into Applications. Both
contain the app and samples; no build tools or extra libraries are required. The
published macOS builds currently target Apple silicon.

The build is the same CMake project; use `release` on a Mac with Xcode's command line tools and
Ninja. The engine is shipped as an application, disk image and Installer package:

```sh
scripts/package-macos.sh                     # configure + build + assemble + .dmg + .pkg
scripts/package-macos.sh --no-build          # reuse build/release
scripts/verify-macos-app.sh                  # check the result the way a user meets it
```

Output, in `build/macos-dist/`: **`YK Engine.app`**,
**`YKEngine-<version>-macos-<arch>.dmg`** (the app and an Applications link), and
**`YKEngine-<version>-macos-<arch>.pkg`** (the macOS Installer wizard). The release workflow
builds and verifies both installers on a real Mac, then attaches them to GitHub Releases.

What the application is, so nothing is missing when it is copied to Applications:

- `Contents/MacOS/yk_editor` is the main executable (`CFBundleExecutable`); `yk_player` and `yk` sit
  next to it. The player is what the editor exports games with, and **Run in Player** starts it.
- `Contents/Resources` holds `AppIcon.icns`, the sample game (`YK-DemoGame`, offered on the welcome
  screen), the license notices and these documents. Nothing is read from the build tree or the
  working directory; the application does not need a terminal, Python, CMake or any script.
- `Info.plist` declares the `.ykproj` document type, so double-clicking a project (or
  `open -a "YK Engine" project.ykproj`) opens it in the editor.
- Logs and crash reports: `~/Library/Logs/YKEngine/Editor/`; settings:
  `~/Library/Application Support/YKEngine/Editor/`. A start-up failure shows a dialog instead of
  closing silently. Quit (Cmd+Q, the Dock menu, logging out) closes the window, ends any player the
  editor started and removes the running marker.

**Signing and notarization.** Without credentials the script signs ad hoc, which is enough to run
the app on the Mac that built it; an application downloaded elsewhere needs Control-click > Open
on its first start. For a release, put the credentials in the environment:

```sh
export YK_CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"   # in your keychain
export YK_INSTALLER_IDENTITY="Developer ID Installer: Your Name (TEAMID)"  # for the .pkg
xcrun notarytool store-credentials yk-notary --apple-id you@example.com --team-id TEAMID   # once
export YK_NOTARY_PROFILE=yk-notary
scripts/package-macos.sh
```

With an identity every program and then the bundle are signed with the hardened runtime, the
entitlements in `packaging/macos/entitlements.plist` and a secure timestamp; with a profile the
`.dmg` and `.pkg` are submitted to Apple, waited for and stapled. **That path has not been run**: it needs an
Apple Developer account, which was not available. Only the ad hoc path is verified (below and in
[STATUS.md](STATUS.md)).

**Architectures.** The script builds for the Mac it runs on (Apple silicon in CI). A universal
binary needs `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` in the `release` preset and both slices of
SDL and Box2D; it has not been tried.

**Exported games** are `GameName.app` bundles made by the exporter (above): the game's own name and
icon, the project in `Contents/Resources/data`, no editor and no engine application needed. The
bundle can be produced on any system for layout checks, but signing and the `.dmg` need a Mac.

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
- *An application closed by itself, or nothing appeared*: read the log (**Help > Open Logs Folder** in
  the editor, or `~/Library/Logs/...` on a Mac); a crash leaves `crash-*.txt` with a stack trace.
  Run the program from a terminal to see stderr too.
- *"YK Engine" is damaged or cannot be opened* (macOS, a build that is not notarized): Control-click
  the app, choose Open, confirm once. Or `xattr -dr com.apple.quarantine "YK Engine.app"`.
- *Behind a proxy*: `scripts/fetch-deps.sh` and `YK_DEPS_DIR` (above).
