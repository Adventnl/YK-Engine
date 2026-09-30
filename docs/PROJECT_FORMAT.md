# Project format

Everything a game is made of is plain text (JSON) or an ordinary media file, so it can be read,
diffed and merged with normal tools. This page describes each file; the running example is the demo
game in [`YK-DemoGame/`](../YK-DemoGame).

Conventions that hold everywhere:

- **Paths are project-relative, with `/`.** `assets/tiles/stone.png`, never `C:\...`, never `..`.
  `Project::resolve` rejects absolute paths and any path that leaves the project; the validator
  reports both.
- **Entities are referenced by id**, 16 hexadecimal digits (`"0a4b110e390f60ec"`). Ids are random,
  so scenes edited independently merge without renumbering.
- **Every document names itself** (`"format"`) and carries a `"version"`. A file that is newer than
  the program reading it is refused with a message; unknown keys are ignored, so older programs
  can still open files that gained optional fields.
- **Loading is strict about structure and tolerant about extras.** A loader names the entity,
  component and property that is wrong and never returns half a document.
- Numbers are doubles, world units are meters (+X right, +Y down), angles are degrees.
- `yk format` (and the editor's Save) write the canonical form: every property present, stable
  order, two-space indentation, a trailing newline. Files written by hand or by scripts may omit
  properties; they load with defaults and are normalized the next time they are formatted.

## A project folder

```text
MyGame/
  project.ykproj              the project: settings, layers, input map, build settings
  scenes/level01.ykscene      scenes (any folder works; the start scene is named in project.ykproj)
  prefabs/hazards/lava.ykprefab
  assets/
    characters/hero.png       textures (.png, .bmp)
    characters/hero.png.ykmeta   optional import settings of that texture
    characters/hero.ykanim    animation clips
    characters/hero.ykctl     animation state machine
    audio/jump.wav            sounds (.wav)
  tools/                      anything else you keep with the project (never exported)
```

The editor creates `scenes/`, `prefabs/` and `assets/`; nothing requires those names. Hidden files
and folders (`.git/`) and folders named `build` are not part of the project's file list.

### A game lives in its own repository

A project is only this folder. It does not have to be inside the engine's checkout, and it does not
know the engine's location: create it anywhere (`yk new path/to/MyGame --name "My Game"`, or **File >
New Project**), open it with the editor or the player by path (or by double-clicking its
`project.ykproj`), and export it. The engine's own repository contains one project, `YK-DemoGame/`,
as a sample and a test; CMake's `YK_DEMO_PROJECT` points the tests, the installation and the macOS
application at a different checkout (or at none) without touching the engine. A game that needs
C++ of its own keeps that code in its repository too and pulls the engine in as a dependency
([BUILDING.md](BUILDING.md#game-modules-custom-c-components)); nothing game-specific is ever added
to `src/` or `include/`. The `external_project` test does this in a temporary folder whose name has
a space and an umlaut: create, validate, play and export from other working directories, then run
the export from a fourth.

## `project.ykproj`

```json
{
  "format": "yk.project",
  "version": 1,
  "name": "Cinder Vale",
  "startScene": "scenes/level01.ykscene",
  "window": { "title": "Cinder Vale - a YK Engine demo", "width": 1280, "height": 720 },
  "layers": [
    { "name": "Default", "interactsWith": ["Default"] },
    { "name": "Solid",   "interactsWith": ["Player", "Prop"] },
    { "name": "Player",  "interactsWith": ["Solid", "Sensor", "Prop"] }
  ],
  "textures": { "pixelsPerUnit": 64, "filter": "linear" },
  "input": { "sets": [ ... ] },
  "build": { "productName": "Cinder Vale", "executable": "CinderVale", "version": "1.0.0" }
}
```

| Key | Meaning |
|---|---|
| `name` | The project's name (the editor's title, the default product name). |
| `startScene` | The scene the player starts with. |
| `window` | Title and size of the game window (at least 160 x 120, at most 16384 pixels a side). The player letterboxes to this logical size. |
| `layers` | Up to 32 named collision layers. `interactsWith` lists the layers each one touches. The relation is symmetric and covers solid collisions and trigger overlaps alike. Components refer to layers **by name**, so adding or reordering layers never retargets existing data. |
| `textures` | Defaults for textures without a sidecar: `pixelsPerUnit` (texture pixels per world unit) and `filter` (`linear` or `nearest`). |
| `input` | Action sets, see below. |
| `build` | Optional. How the game is named and what is left out when exported, see below. Absent when never touched. |

### Input map

Gameplay asks for **actions** by name; the input map says which keys and gamepad inputs drive them.
An **action set** is one controller: a person on the keyboard, a gamepad, a second player.

```json
"input": {
  "sets": [
    { "name": "Player1", "gamepad": 0,
      "actions": [
        { "name": "MoveLeft",  "bindings": ["Key:A", "Pad:DPadLeft", "PadAxis:LeftX-"] },
        { "name": "MoveRight", "bindings": ["Key:D", "Pad:DPadRight", "PadAxis:LeftX+"] },
        { "name": "Jump",      "bindings": ["Key:W", "Pad:South"] }
      ] },
    { "name": "Player2", "gamepad": 1, "actions": [ ... arrows and the second pad ... ] }
  ]
}
```

- `gamepad` is the pad slot (0 to 3) the set also listens to, or `-1` for none.
- A binding is `Key:<name>`, `Pad:<button>` or `PadAxis:<axis><sign>`. Any one binding holds the
  action. `PadAxis:LeftX-` means the left half of that stick axis.
- Key names: `A`-`Z`, `0`-`9`, `Left` `Right` `Up` `Down`, `Space` `Enter` `Escape` `Tab`
  `Backspace` `Delete` `Insert` `Home` `End` `PageUp` `PageDown`, `LeftShift` `RightShift`
  `LeftCtrl` `RightCtrl` `LeftAlt` `RightAlt`, `Minus` `Equals` `Comma` `Period` `Slash`
  `Semicolon` `Apostrophe` `LeftBracket` `RightBracket` `Grave` `Backslash`, `F1`-`F12`.
- Pad buttons: `South` `East` `West` `North` `Back` `Start` `LeftStick` `RightStick`
  `LeftShoulder` `RightShoulder` `DPadUp` `DPadDown` `DPadLeft` `DPadRight`. Axes: `LeftX` `LeftY`
  `RightX` `RightY` `LeftTrigger` `RightTrigger`.
- Set names and action names must be unique (actions within their set). Components refer to a
  set through a `PlayerInput` component and to actions by name (`Lever.interactAction`).

A new project starts with `Player1` (WASD, pad 0), `Player2` (arrow keys, pad 1) and `Global`
(restart, pause, continue: Enter or the pad's East button, which `LevelFlow.continueAction` uses to
skip the wait after a level ends). Edit the map in the editor: **File > Project Settings > Input**.
A project saved before `Continue` existed simply has no such action; asking for an action a project
does not define reads as "not pressed", so nothing breaks.

### Build settings

```json
"build": {
  "productName": "Cinder Vale",      // shown to players; empty: the project name
  "executable": "CinderVale",        // program file name without extension; empty: derived
  "version": "1.0.0",
  "identifier": "com.studio.cindervale",   // macOS bundle id; empty: com.yk.<product>
  "icon": "assets/icon.png",         // square PNG, 512 x 512 or larger; empty: no icon
  "copyright": "Copyright 2026 Studio",    // shown in the macOS "About"; empty: none
  "exclude": ["art/source", "notes.txt"]   // project-relative files or folders left out
}
```

`icon` is checked by validation (it must exist and be a square PNG of at least 128 pixels; under 512 is a warning, since a Retina display shows it sharper larger).
The exporter turns it into `AppIcon.icns` for a macOS bundle and the player uses it as the window
icon on Windows and Linux. Both keys are written only when set, so older projects are unchanged.

Development folders never ship whatever the list says: `tools/`, `docs/`, `build/`, scripts
(`*.py`), notes (`*.md`), backup and scratch files (`*.bak`, `*.tmp`, `*.orig`, `*~`). See
[Exported games](#exported-games).

## Scenes: `*.ykscene`

```json
{
  "format": "yk.scene",
  "version": 1,
  "settings": { "name": "Room 1", "gravity": [0, 9.81], "background": "#08141aff" },
  "entities": [
    {
      "id": "d0c992a2af7f6364",
      "name": "Camera",
      "transform": { "position": [13, 7], "rotation": 0, "scale": [1, 1] },
      "components": [
        { "type": "Camera",
          "properties": { "primary": true, "mode": "FitTargets", "targets": ["bb60b08b3b3597f9"] } }
      ]
    },
    {
      "id": "a01fdae0ac3baaac", "name": "Backdrop", "locked": true,
      "prefab": "prefabs/level/backdrop.ykprefab",
      "transform": { "position": [13, 7], "rotation": 0, "scale": [1, 1] },
      "components": []
    },
    { "id": "0a4b110e390f60ec", "name": "Far Wall", "parent": "a01fdae0ac3baaac", ... }
  ]
}
```

Scene `settings`: `name`, `gravity` (m/s², +Y down) and `background` (`#rrggbbaa`).

Entity record:

| Key | Meaning |
|---|---|
| `id`, `name` | Required. Names need not be unique; ids are. |
| `parent` | The parent's id. Absent for top-level entities. Entities are listed in hierarchy order, so siblings keep their order. |
| `active` | `false` switches the entity and everything below it off (the game ignores it). Absent means active. |
| `tags` | Strings that gameplay filters on (`"tags": ["fire"]`). |
| `locked`, `editorHidden` | Editor hints only: not selectable/movable, not drawn in the scene view. The running game ignores both. |
| `prefab` | On the root of a prefab instance: the prefab it was created from. |
| `transform` | Local position, rotation (degrees) and scale, relative to the parent. |
| `components` | The entity's components, in order (dependencies first). |

A component record is `{"type": "<registered name>", "properties": {...}}` (plus `"enabled": false`
for a disabled one). `properties` holds every field the component declares; the encoding follows
the field's type:

| Field type | JSON |
|---|---|
| bool, int, float | number or boolean; ranges are enforced on load (clamped) |
| string | string |
| vector | `[x, y]` |
| color | `"#rrggbbaa"` |
| enum | the option name (`"FitTargets"`); an integer index is accepted |
| entity reference | the entity's id, or `""` for none |
| entity list | array of ids |
| string list | array of strings |
| asset | a project-relative path (`"assets/audio/jump.wav"`), a built-in (`"builtin:circle"`) or a procedural sound (`"tone:440,0.1,square"`); `""` for none |

Runtime-only state is declared read-only and never appears in a file. The complete list of
components and their fields is [components.md](components.md), generated from the same registry
the editor's Inspector uses (`yk components`). A test fails when it is out of date.

References: an entity reference that names an entity the scene does not have is reported by the
validator and logged as a warning on load, and behaves as "no reference" at run time; it never
crashes.

## Prefabs: `*.ykprefab`

A prefab is an entity subtree that can be placed any number of times.

```json
{
  "format": "yk.prefab",
  "version": 1,
  "root": "23472b3e2099f66a",
  "entities": [
    { "id": "23472b3e2099f66a", "name": "Lever", "transform": { ... }, "components": [ ... ] },
    { "id": "5d0f...", "name": "Handle", "parent": "23472b3e2099f66a", ... }
  ]
}
```

- The entity records have the same shape as in a scene. The root has no `parent`; the ids in the
  file are only used to tie the records together, so placing a prefab gives every entity **a fresh
  id** and remaps references between them. References to entities outside the subtree are cleared
  (they mean nothing in another scene).
- A prefab never names itself: the root record has no `prefab` key. (An instance of another prefab
  inside a prefab keeps its link.)
- **Instances are full copies that remember their source.** Placing a prefab copies its entities
  into the scene and records the prefab's path in the `prefab` key of the copy's root. The scene
  file therefore contains everything the game needs, and the link is only used by the editor:
  *Revert to Prefab* puts an instance back to the file's contents (its identity, name and
  placement stay, links from other entities to it stay valid, and wiring to entities outside the
  instance survives), *Apply to Prefab* writes an instance into the file, *Update Other Instances*
  reverts all the others. Instances carry no override data, so those operations replace an
  instance's own changes; the editor asks before it does that in bulk.
- The validator warns when an instance's prefab file is missing or is not a `.ykprefab`.

## Animation clips: `*.ykanim`

```json
{
  "format": "yk.animation", "version": 2,
  "texture": "assets/characters/ember.png", "columns": 8, "rows": 4,
  "clips": [
    { "name": "idle", "frames": [0, 1, 2, 1], "fps": 6 },
    { "name": "run",  "first": 8, "count": 8, "fps": 14 },
    { "name": "land", "first": 16, "count": 3, "durations": [0.05, 0.05, 0.1], "loop": false, "next": "idle" },
    { "name": "step", "first": 24, "count": 4, "fps": 10,
      "events": [ { "frame": 1, "name": "footstep", "sound": "assets/audio/step.wav" } ] }
  ]
}
```

A sprite sheet layout plus clips. A clip is `first` + `count` consecutive cells or an explicit
`frames` list; timing is `fps` or a per-frame `durations` list (seconds). `loop` defaults to true;
a clip that does not loop can continue with `next`. `events` raise a named event (and optionally
play a sound) when a frame is entered. Version 1 files (no `texture`, only `first`/`count`/`fps`)
still load. Cells are numbered row by row, starting at 0.

## Animation state machines: `*.ykctl`

```json
{
  "format": "yk.animator", "version": 1,
  "parameters": [ { "name": "speed", "type": "float" },
                  { "name": "grounded", "type": "bool", "default": true },
                  { "name": "jumped", "type": "trigger" } ],
  "entry": "Idle",
  "states": [ { "name": "Idle", "clip": "idle" },
              { "name": "Run", "clip": "run", "speedParameter": "speedRatio" },
              { "name": "Jump", "clip": "jump" } ],
  "transitions": [
    { "from": "Idle", "to": "Run", "when": [ { "parameter": "speed", "op": ">", "value": 0.2 } ] },
    { "from": "*", "to": "Jump", "when": [ { "parameter": "jumped" } ] },
    { "from": "Land", "to": "Idle", "exitTime": 1.0 }
  ]
}
```

Gameplay publishes generic parameters (`speed`, `grounded`, `facing`, the triggers `jumped` and
`landed`, ...); the controller decides which clip plays. Transitions from `"*"` (any state) are
checked first, then those of the current state, each group in file order; the first whose
conditions all hold (and whose `exitTime` fraction of the clip has played) fires. A condition with
no `op` means a bool or trigger is true. An `AnimatedSprite` component names the `.ykanim` and the
`.ykctl`; it mirrors the sprite while its `flipParameter` (default `facing`, which the platformer
controller publishes) is negative, or positive when the art is drawn facing left (`artFacesLeft`).

## Texture import settings: `<texture>.ykmeta`

```json
{ "format": "yk.texture", "version": 1,
  "pixelsPerUnit": 32, "filter": "nearest", "columns": 8, "rows": 4, "border": [8, 8, 8, 8] }
```

Every field is optional and falls back to the project's `textures` defaults, so most textures have
no sidecar. `pixelsPerUnit` gives a texture its world size; `columns`/`rows` are the default
sprite-sheet grid; `border` (left, top, right, bottom, in pixels) is the nine-slice frame of a
`Sliced` sprite. The editor writes and removes the sidecar from the Inspector (select the texture
in the Explorer); a texture with all settings cleared has no sidecar.

## Exported games

`yk export` and **Build > Export Game** produce a folder (or a macOS bundle) that runs without the
editor:

```text
Windows, Linux:                         macOS:
Cinder-Vale-windows/                    Cinder Vale.app/Contents/
  CinderVale.exe    the player            MacOS/CinderVale       the player
  data/             the project           Resources/data/        the project
  licenses/         notices               Resources/licenses/    notices
  README.txt                              Resources/AppIcon.icns the game's icon
  yk-export.json                          Resources/README.txt
                                          Resources/yk-export.json
                                          Info.plist
```

On a Mac the exporter can also sign the bundle (`--sign`: ad hoc, or a Developer ID identity with
the hardened runtime) and write `Cinder Vale.dmg` (`--dmg`: the app and an Applications link). It
runs `codesign` and `hdiutil` and checks the signature afterwards; without those tools the option
is refused with the reason, not skipped.

`data/` holds `project.ykproj` and every project file except the ones listed under
[Build settings](#build-settings). The player looks for `data/` (or `project/`) next to itself and
otherwise for a project in the current folder; `--project <dir>` overrides that. `yk-export.json`
records the tool version, target, product and file count; it is also how a later export knows it
may replace this folder. The exported copy is validated on its own before an export is reported as
done.

**At run time** the exported program finds `data/` beside itself (for a macOS bundle:
`Contents/Resources`, from the program's real path, not from the working directory), so it works
from any folder, from Finder or from a terminal, and from a path containing spaces. Its log and
crash reports go to the system's per-user log folder under the game's executable name
(`~/Library/Logs/CinderVale/player.log` on a Mac), never into the game folder. If the game cannot
start (no `data/`, a project that does not load, no display), it says so in the log, on stderr and (when
there is a display) in a dialog window, and exits with a non-zero status; there is no silent exit.

## Editor files (not part of a project)

The editor keeps its own settings in the per-user data folder (or the folder given with
`--settings-dir`): `recent.json` (recently opened projects) and `workbench.json` (side bar and
panel sizes and views, split editor, inspector visibility). Both are tolerant on load: a missing
or damaged file is replaced by defaults. Nothing in a project depends on them.
