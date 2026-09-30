# Cinder Vale: the YK Engine demo game

A two-player co-op puzzle platformer, built to test the engine. **Ember** (WASD) and **Tide** (arrow
keys) must both reach their exit. Each burns or drowns in the other's element, goo kills both, and
every crossing needs the other player: a lever opens a gate, a platform hovers over lava, a second
platform runs only while someone stands on a plate.

This folder is an ordinary YK project. It contains **no C++**: everything the game does comes from
the engine's generic components (`Hazard`, `Collectible`, `Goal`, `Door`, `PressurePlate`,
`MovingPlatform`, `PlatformerController`, `Killable`, `LevelFlow`, `ParticleEmitter`...), configured in
prefabs and scenes. "Lava", "exit door" and "gem" exist only as data here; the engine knows none of
them.

## Playing it

```
yk_editor YK-DemoGame          open it in the editor (Play button, or F5)
yk_player YK-DemoGame          play it without the editor
```

| | Ember | Tide |
|---|---|---|
| Move | A / D | Left / Right |
| Jump | W | Up |
| Use (lever) | S or E | Down or Right Ctrl |

R restarts the level; Enter (pad: East) skips the wait after a level is completed. Gamepads work too:
pad 1 drives Ember, pad 2 drives Tide (edit the bindings in `project.ykproj`, section `input`).

## What is in it

```
project.ykproj      window, collision layers, input map, texture defaults
scenes/level01      Room 1 (44 x 19 units): the level
scenes/practice     a small practice room; completing either room leads into the other
prefabs/            terrain, hazards, items, characters, mechanisms, decor, fx, level parts
assets/             sprite sheets, animations (.ykanim), state machines (.ykctl), textures, audio
tools/              Python that generated the art, prefabs and levels (see below)
```

Collision layers: `Solid` (terrain, gates, platforms), `Player`, `Sensor` (triggers), `Prop` (the
crate). Players do not collide with each other. Sprite layers (`SpriteRenderer.layer`): far and mid
backdrop -100/-90, hanging decor -20, gate 0, terrain 10, plants 12, mechanisms 20, crate 35,
characters 40, liquids 50 (so a character wading in lava is drawn behind it), gems 55, particles 60,
glows 70.

## Regenerating the content

The art, the audio, the prefabs and both scenes are produced by scripts, so they can be reviewed as
code and changed in one place. The generated files are committed: **you do not need Python to run or
edit the game.** Once generated they are plain data files like any others, and the editor is the
intended way to change them afterwards.

```
python3 tools/build_content.py --yk <build>/yk            prefabs + scenes, then `yk format` + `yk validate`
python3 tools/build_content.py --art --yk <build>/yk      also the art and audio (needs Pillow and numpy)
```

* `tools/art/` draws everything from scratch with Pillow (supersampled vector-style drawing): the
  characters (idle, run, jump, fall, land, interact and death frames, one shared layout so one state
  machine drives both), tiles, animated hazards, gems, mechanisms, plants, backdrops, and synthesizes
  every sound effect and the music loop with numpy. There is no third-party art or audio.
* `tools/build_prefabs.py` writes every prefab from the engine's component set.
* `tools/build_level.py` places prefabs into `level01` and `practice` and wires the mechanisms
  together (which lever opens which gate, which plates drive which platform).
* `yk format` rewrites the result in the editor's canonical form, so the files diff cleanly against
  ones saved by the editor.

## The level, in order

1. **The hill and the lava.** Ember wades through the lava (gems lie on its floor). Tide climbs the
   hill and jumps onto the platform hovering over it. Both use the ramp (slopes, ground snapping).
2. **Lever and gate.** Stand next to the lever and press Use. The gate stays open.
3. **The water.** Tide wades through it; Ember takes the jump-through ledges above. A checkpoint sits
   before it: a dead player comes back there.
4. **The stairs** of one-way ledges lead to the upper floor.
5. **The goo pit.** The only way across is a platform that moves while either plate is held. The
   plates are real buttons: stand on one and it sinks under you, jump onto it from above and it takes
   your weight, push the crate onto it and it stays down. One player holds the near plate while the
   other rides across and holds the far one; then the first follows.
6. **The exits.** Both players stand in their door at the same time: the controls lock, each walks
   into their door and fades away, LEVEL COMPLETE shows, and the screen fades into the practice
   room (Enter skips the wait). Completing that room leads back here.

## How the mechanisms are built

Nothing here is special to this game; each is the engine's component set configured in a prefab:

* **Plates** (`prefabs/mechanisms/plate`): a root with `PressurePlate` and a child `Pad` holding a
  kinematic `RigidBody` and a solid `Collider` with a chamfered rim (so the crate can be pushed up
  onto it), plus the art. The plate senses the weight resting on its pad and sinks 8 cm, carrying
  whatever stands on it; `pressed` and `pressAmount` drive its animation.
* **Gates** are `Door` on a kinematic body (they slide up and stop if a player is under them).
  **Platforms** are `MovingPlatform`, easing in and out so riders stay on.
* **Levers** are `Lever` with an interact action; **exits** are `Goal`s inside a `LevelFlow`, which
  also holds the completion, retry and next-scene rules.

`tests/unit/demo_tests.cpp` (in the engine repository) plays exactly this with a scripted bot, so a
change to the engine or the level that breaks the route fails the build.
