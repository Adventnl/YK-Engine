# Elemental Escape foundation

`elemental_escape` is the integrated two-area reference game and the starting point for a
Fireboy-and-Watergirl-style cooperative puzzle game. The current slice has one controllable
character; a second character, elemental switches/liquids, controller input, and cooperative
puzzles remain future gameplay work.

## Build, run, and package

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/elemental_escape
cmake --preset release && cmake --build --preset release
cmake --install build/release --prefix build/package/ElementalEscape
```

The executable resolves `assets` beside itself, rather than from the working directory.
Linux saves use SDL's pref path (`~/.local/share/YK/Elemental Escape/progress.yks` on a
conventional desktop). Windows and macOS use SDL's corresponding user-data location. Pass an
absolute asset directory as the sole argument only for development. `--smoke` runs three frames,
captures `elemental-escape-smoke.bmp`, and exits.

The source tree stores the tiny placeholder sprite sheet as ASCII Base64 in
`assets/sprites/player.png.base64`, because the review transport does not accept binary files.
CMake invokes `scripts/generate_assets.py` with Python 3 to validate and decode it into the build
tree; only the decoded `player.png` is copied into runtime and release packages. Python is therefore
a build-time prerequisite, not a runtime prerequisite.

Keyboard: A/D or arrows move, Space jumps, Down+Space drops through a one-way platform, E
interacts, Escape pauses/resumes, and Q returns to the title from pause or quits from title.
The title starts or continues with Space/E. UI input owns its frames, so it cannot move gameplay.
Audio uses a generated event tone and degrades to silence when SDL has no audio device.

## Level format

Levels are finite text maps in `assets/levels`. Header fields are ID, width, height, and tile
size. Exactly `height` collision rows of exactly `width` cells follow.

```text
level example 8 5 32
collision
........
........
..====..
.../\\...
########
spawn start 48 128
entity key stable_key 96 96 20 20
entity transition exit 220 64 32 64 other_level 48 128
```

Collision glyphs are `.` empty, `#` solid, `=` one-way, `/` 45-degree rising-left slope, `\\`
45-degree rising-right slope, and `!` hazard. Visuals and collision remain separate in runtime:
the demonstration colors collision types, while entities form the object layer. Entity forms are
`key`, `door`, `checkpoint`, `transition`, `platform`, and `decoration`; all IDs must be authored,
unique, and stable. Transitions name a level and an exact authored destination spawn position.
The loader rejects malformed dimensions, glyphs, numbers, IDs, duplicate persistent IDs,
embedded spawns, and broken links before activation.

## Runtime boundaries

`GameFoundation.hpp` owns validated level data, fixed-tick input buffering, animation playback,
the authoritative character state/controller, and versioned durable progress. The application
owns the 60 Hz accumulator, caps a rendered frame to 250 ms and eight catch-up ticks, and drops
excess backlog. Held direction reaches every tick; action edges are consumed once. Focus loss is
cleared by `Application`; pause clears both the accumulator and buffered input.

World coordinates use +X right/+Y down and pixels as game-world units. The player position is the
feet/pivot; its fixed 18x30 collision body never changes with animation. Controller tuning is in
`ControllerConfig` in `include/yk/core/GameFoundation.hpp`. Collision queries only visit the tile
range overlapped by the player. PNGs decode to cached RGBA textures with nearest filtering;
atlas regions, pivots, flipping, tint, opacity, scale, and render layers are supported by `Sprite`.
Animation advances only on fixed ticks.

Saves contain version 1, current level/checkpoint spawn, inventory count, collected IDs, unlocked
IDs, and checkpoint IDs. They contain no runtime handles. Writes use a same-directory temporary
file and replacement. Invalid/future saves are not applied or overwritten; the title safely offers
a new game.

## Verified scope and limits

The Linux package is locally built and smoke-run with SDL's dummy video/audio drivers. The custom
controller handles solids, 45-degree slopes, one-way landing/drop-through, moving-platform landing,
carry and detachment, hazards, and bounded fixed-step input. The demo has event sound effects but
not streamed music, rebindable settings, or
volume UI. Text is an intentionally small cached bitmap font. Windows/macOS source support is
expected through existing SDL/CMake boundaries but is unverified in this continuation.
