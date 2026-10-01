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
(restart; pause, which the game session handles for the player and the editor's Play mode alike;
continue: Enter or the pad's East button, which `LevelFlow.continueAction` uses to skip the wait
after a level ends). Edit the map in the editor: **File > Project Settings > Input**.
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

## Definition files: the content of a simulation game

Stats, status effects, items, loot, recipes, quests, conversations and cutscenes are data, not code.
A project holds any number of these files anywhere under it; the running game, `yk validate` and the
editor's Problems panel all read them the same way, with the same messages
([ADR 0021](decisions/0021-definitions-are-versioned-data-in-gamedata.md)).

| Extension | Holds | Read by |
|---|---|---|
| `.ykdata` | any of the sections `stats`, `effects`, `items`, `lootTables`, `lootPools`, `recipes`, `quests`, `factions`, `schedules`, `tables` | `GameData` |
| `.ykitem`, `.ykrecipe`, `.ykquest`, `.ykschedule` | the same sections, or just one definition (an object with an `id`) | `GameData` |
| `.ykloot` | `tables` and `pools` | `GameData` |
| `.ykdialogue` | a list of pages, or a graph of nodes | the `Dialogue` component |
| `.ykseq` | a cutscene: a timeline of cues | the `SequencePlayer` component |
| `.yktileset` | the properties of the tiles of a sheet | `Tileset`, see [ADR 0022](decisions/0022-tilemaps-are-a-component-with-chunked-layers.md) |

Rules that apply to all of them:

- Ids are `[a-zA-Z0-9_-]` and are how everything refers to everything else (an item in a recipe, a
  quest in a condition). Nothing saved holds a pointer or an index.
- `format` and `version` are optional; a `version` newer than the engine reads is an error. An unknown
  key is a **warning**, so that a typo is never silently ignored.
- One bad definition does not stop the others loading: each problem is reported with its file.
- Conditions (`"if"`, `"requires"`, `"complete"`) and actions (`"actions"`, `"rewards"`, `"onApply"`)
  are the one rule language ([ADR 0018](decisions/0018-one-condition-and-action-language.md)). A
  condition is `{"type": "HasItem", "item": "key"}`, `{"var": "alarm"}`, `{"fact": "actor.stat.health",
  "op": "<", "value": 20}`, or `{"all": [...]}`, `{"any": [...]}`, `{"not": ...}`; an action is
  `{"type": "GiveItem", "item": "key", "count": 1}`. The Inspector lists every condition and action
  the project's registry knows, with its parameters; `yk validate` checks each one (a mistyped type,
  a missing parameter, an item that does not exist).

### Stats, effects and items

```json
{ "format": "yk.data", "version": 1,
  "stats": [
    { "id": "health",  "max": 100, "start": "max", "thresholds": [{ "at": 25, "event": "health.low" }] },
    { "id": "stamina", "max": 50,  "start": "max", "regen": 10, "regenDelay": 1 },
    { "id": "strength", "max": 100, "start": 10 },
    { "id": "money",   "max": 9999, "start": 0, "integer": true } ],
  "effects": [
    { "id": "poisoned", "duration": 4, "stacking": "stack", "maxStacks": 3,
      "ticks": [{ "stat": "health", "amount": -2, "every": 1, "type": "poison" }],
      "factors": { "move.speed": 0.8 }, "flags": ["no_sprint"], "tags": ["debuff"] } ],
  "items": [
    { "id": "sandwich", "displayName": "Sandwich", "stackSize": 5,
      "use": { "actions": [{ "type": "Heal", "amount": 10 }], "consume": true, "cooldown": 2 } },
    { "id": "guard_outfit", "tags": ["outfit", "contraband"],
      "equip": { "slot": "Outfit", "modifiers": [{ "stat": "strength", "add": 3 }],
                 "flags": ["disguise.guard"], "grants": ["access:guard"],
                 "appearance": { "outfit": "assets/char/guard.png" } } } ] }
```

A stat's `start` is a number, `"max"` or `"min"`; a negative `regen` drains. An effect's `modifiers`
change a stat's `value`, `max`, `min` or `regen` channel (`add`, `mult`); `factors` are named
multipliers other systems read (`move.speed`, `damage.taken`); `flags` are names other systems ask
about. Wearing an item that has `equip` applies the effect `equip:<item id>`, which the engine
builds for it. The full shapes are documented where they are read: `include/yk/stats/Stats.hpp`,
`include/yk/items/Items.hpp`, `include/yk/items/Loot.hpp` and `include/yk/items/Crafting.hpp`.

### Factions: `"factions"` in a `.ykdata`

```json
{ "factions": [
    { "id": "guards", "name": "Guards", "default": "neutral",
      "relations": { "inmates": "suspicious", "dogs": "hostile" } },
    { "id": "inmates", "name": "Inmates", "relations": { "guards": "neutral", "medics": "friendly" } },
    { "id": "medics", "name": "Medics", "default": "friendly" },
    { "id": "dogs", "default": "hostile", "relations": { "guards": "friendly" } } ] }
```

A faction says how it regards the others: `friendly`, `neutral`, `suspicious` or `hostile`, with
`default` for the ones it does not mention (neutral unless said; members of one faction are friendly
to each other). The regard is not mutual unless both say so. The engine knows none of these names:
a game's own factions are just data. In a scene, an `Identity` component gives a character its
persistent id (`"npc.warden"`, unique in the world, leave it empty on a prefab), its `faction` and
its `role`; a `Relationships` component adds personal feelings (opinion, trust, hostility, by
persistent id) that can make someone a friend or an enemy despite their factions. The rule
predicates `Relation`, `InFaction` and `HasRole`, the facts `identity.*` and `relationship.*`, and
the actions `ChangeRelationship`, `SetFaction` and `SetRole` work with them. A status effect with the
flag `disguise.<faction>` makes its wearer look like a member of that faction to anything that asks
for the `perceived` relation.

### Schedules: `*.ykschedule`, or `"schedules"` in a `.ykdata`

```json
{ "schedules": [
    { "id": "inmate", "name": "Inmate routine", "roles": ["inmate"], "blocks": [
        { "id": "wake", "from": "06:00", "to": "07:00", "activity": "Wake up",
          "destination": "home", "behavior": "idle" },
        { "id": "roll", "from": "07:00", "to": "08:00", "activity": "Roll call",
          "destination": { "zone": "lineup" }, "behavior": "stand", "tolerance": 5,
          "requires": { "enter": "zone:lineup", "stay": 30 }, "tags": ["mandatory"],
          "onStart": [{ "type": "SetVariable", "name": "roll_call", "value": true }] },
        { "id": "lunch", "from": "12:00", "to": "13:00", "activity": "Lunch",
          "destination": "purpose:dining", "behavior": "eat" },
        { "id": "visits", "from": "09:00", "to": "10:00", "activity": "Visiting hour",
          "days": [5, 6], "behavior": "visit" },
        { "id": "night", "from": "22:00", "to": "06:00", "activity": "Lights out",
          "destination": { "point": [2, 3] }, "behavior": "sleep" } ] } ] }
```

A schedule is a routine over the day, as data: each block has a `from` and `to` (a block that ends
before it starts wraps past midnight), an `activity` (a label for people and rules), a `destination`
(`home`, a zone, a room, a `purpose` any room or zone can have, an entity or a point; the block's
AI decides how to get there), a `behavior` (the word the character's AI maps to what it does there),
a `tolerance` in minutes before arriving late, optional `days` (weekday numbers 0-6, counting from
the game's first day), `priority` where blocks overlap, `requires` (for a routine that is enforced,
the player's: be in a place, stay a while, do something) and rule actions to run when it starts and
ends. Times with no block are free time. A character follows a schedule through a `ScheduleAgent`
component, the one named or the one that lists its `roles`. The world clock behind it is a
`ClockSettings` component on any entity (a scene may carry one; without it the clock starts on day 1
at 06:00 and runs a minute of the world per second): `minutesPerSecond`, `startDay`, `startTime`,
`startPaused`, `dayStarts`, `nightStarts`. Rules read the clock as the facts `clock.hour`,
`clock.minute`, `clock.day`, `clock.weekday`, `clock.time`, `clock.daylight`..., test it with the
predicates `TimeBetween` and `IsDaylight`, change it with `SetTime`, `SkipTime`, `PauseClock`,
`ResumeClock` and `SetClockScale`, and react to the events `clock.minute`, `clock.hour`, `clock.day`
and `clock.daylight`: `{"when": "clock.minute", "data": {"time": "06:00"}, "then": [...]}` runs at
06:00. A `ScheduleAgent` raises `schedule.block_started`, `schedule.block_ended`,
`schedule.arrived` and `schedule.late` for the rules to answer, and its state is read with the facts
`schedule.activity`, `schedule.block`, `schedule.next`, `schedule.minutesLeft`, `schedule.late`...

Where a block sends a character is found through **zones**: a `Zone` component on an entity (a box,
circle or polygon in the scene) gives a place an `id`, a `name`, `tags`, `purposes` ("dining",
"sleep"), a `priority` where zones overlap, a `capacity`, who may be in it (`allowedFactions`,
`allowedRoles`, an `access` condition, the `owner`'s persistent id, `countDisguise`) and
`environment` flags (`{"dark": true}`). A `room` is a zone that is also a place to be sent to, owned
and searched. `"destination": {"zone": "yard"}`, `{"room": "cafeteria"}` and
`"purpose:dining"` (the nearest zone with that purpose and a free place) resolve to a spot inside;
a `ScheduleAgent` notices by itself when its character is at the destination (`schedule.arrived`),
and with `enforce` it checks the block's `requires`: it must **enter** a place, **stay** there for
some seconds without leaving, or **do** something (an event of that name raised by the character),
and raises `schedule.requirement_met` or, at the end of the block, `schedule.requirement_missed` for
rules to punish. Entering and leaving are `zone.entered` and `zone.exited`; entering a zone without
access, when the zone `enforce`s it, is `zone.trespass` (its `trespassViolation` names what it is for
the violation system). The facts `zone.id`, `zone.room`, `zone.purpose`, `zone.restricted`,
`zone.env.<flag>` and the predicates `InZone`, `ZoneAllows` and `ZoneOccupied` ask where someone is.
A zone's `navigationArea` and `navigationCost` shape the navigation grid: an agent that forbids the
area walks around it.

### Security: `"security"` in a `.ykdata`

Alert levels and lockdowns, with nothing about doors or guards built in: every effect is a list of
rule actions, and AI brains and access conditions read the state as facts (`security.level`,
`security.levelId`, `security.lockdown`, `security.remaining`) or predicates (`SecurityLevel`,
`LockdownActive`).

```json
{ "security": {
    "levels": [
      { "id": "normal", "name": "Normal" },
      { "id": "alert", "name": "Increased patrol", "decay": 120,
        "onEnter": [ { "type": "SetVariable", "name": "patrols_doubled", "value": true } ],
        "onExit":  [ { "type": "SetVariable", "name": "patrols_doubled", "value": false } ] },
      { "id": "lockdown", "name": "Lockdown" } ],
    "lockdowns": [
      { "id": "riot", "name": "Riot", "countdown": 90, "level": "lockdown",
        "onStart": [ { "type": "SetVariable", "name": "doors_sealed", "value": true } ],
        "onEnd":   [ { "type": "SetVariable", "name": "doors_sealed", "value": false } ],
        "onFail":  [ { "type": "EmitEvent", "event": "riot.lost" } ] } ] } }
```

- Levels are ordered; `SetSecurityLevel` takes an id or a number, `RaiseSecurity {by}` moves up or
  down and stops at the ends. Leaving a level runs its `onExit`, entering runs `onEnter`, and
  `security.changed` is raised. `decay` is the seconds without a raise before it falls one level.
- `StartLockdown` / `EndLockdown`: the lockdown's `level` is entered, `countdown` seconds count down
  (`lockdown.countdown` each second) and when they run out `onFail` runs and `lockdown.ended` is
  raised with `failed: true`. Only one lockdown runs at a time.
- An **`AccessPolicy`** component on a door, terminal or gate says who may use it: `allowedFactions`
  (by what the user looks like, unless `countDisguise` is off), `allowedRoles`, an `access`
  condition (a keycard, a quest, the time of day, the security level) and `lockedDuringLockdown`.
  Rules ask with `AccessAllowed {target, entity}`.

### Quests: `*.ykquest`, or `"quests"` in a `.ykdata`

```json
{ "id": "repair_autopilot", "title": "Fix the autopilot", "giver": "npc.mechanic",
  "requires": { "type": "QuestState", "quest": "meet_mechanic", "state": "complete" },
  "objectives": [
    { "id": "get_board", "text": "Find a circuit board", "complete": { "type": "HasItem", "item": "circuit_board" } },
    { "id": "repair", "text": "Repair the console", "after": ["get_board"], "manual": true,
      "onComplete": [{ "type": "SetVariable", "name": "autopilot_repaired", "value": true }] },
    { "id": "polish", "text": "Polish the panel", "optional": true, "manual": true } ],
  "onStart": [{ "type": "SetVariable", "name": "quest_started", "value": true }],
  "rewards": [{ "type": "GiveItem", "item": "coin", "count": 5 }],
  "timeLimit": 600, "fail": { "var": "alarm" }, "onFail": [], "repeatable": false, "autoStart": false }
```

An objective is complete when its condition (`"complete"`) holds, when an event has happened
`count` times (`"on": {"event": "defeated", "data": {"kind": "guard"}}`, with an optional `"if"`), or
when an action says so (`"manual": true`, then `CompleteObjective`). `after` makes an objective wait
for others; `optional` ones are not needed; `hidden` ones are not shown until active. A quest is
complete when its required objectives are, or when `"complete": "any"` / a condition says so.
A character's `QuestLog` component holds the state and raises `quest.started`, `quest.completed`,
`quest.failed` and `objective.completed`; the predicates `QuestState` and `ObjectiveComplete` and
the actions `StartQuest`, `CompleteQuest`, `FailQuest`, `ResetQuest`, `CompleteObjective` and
`ProgressObjective` work with it from any rule. The facts `actor.quest.<id>` and
`actor.quest.<id>.progress.<objective>` can be compared in conditions.

### Conversations: `*.ykdialogue`

The old linear form still works: `{"pages": ["Hello.", {"speaker": "Mara", "text": "Hi", "portrait": "assets/mara.png"}]}`.
A graph has nodes. A node has a speaker, text (with `{variable}` placeholders), a portrait or an
expression, actions that run when it is shown, and a way on: `choices` the player picks from, `branch`
conditions (the first that holds), or a plain `next`. A node with no text only runs its actions and
goes on, which makes it a place for logic.

```json
{ "start": "greet",
  "speakers": { "mara": { "name": "Keeper Mara", "side": "left",
      "portraits": { "default": "assets/mara.png", "smile": "assets/mara_smile.png" } } },
  "nodes": {
    "greet": { "speaker": "mara", "text": "Hello, {player_name}.",
      "choices": [
        { "text": "I need a screwdriver.", "if": { "type": "HasItem", "item": "favor_token" },
          "actions": [{ "type": "GiveItem", "item": "screwdriver" }], "next": "thanks" },
        { "text": "Who are you?", "once": true, "next": "who" },
        { "text": "Goodbye." } ] },
    "who":    { "speaker": "mara", "expression": "smile", "text": "I keep the keys.", "next": "greet" },
    "thanks": { "speaker": "mara", "text": "Do not let anyone see it.",
                "branch": [{ "if": { "var": "owes" }, "next": "debt" }, { "next": "farewell" }] },
    "debt":     { "speaker": "mara", "text": "You still owe me." },
    "farewell": { "speaker": "mara", "text": "Stay safe." } } }
```

A choice without `next` ends the conversation; `once` removes a choice for good after it is picked;
`side` is `left` or `right`. `yk validate` reports unreachable nodes, references to nodes and speakers
that do not exist, missing portraits and every condition and action inside.

### Cutscenes: `*.ykseq`

A sequence is a short timeline: a list of **cues**, each at a time in seconds on the sequence's own
clock. It is not a track editor; it is enough to fade, pan the camera, walk a character, talk, wait
for something to happen and run any rule action at the right moment.

```json
{ "lockInput": true, "skippable": true,
  "cues": [
    { "time": 0,   "type": "Fade", "to": 1, "seconds": 0 },
    { "time": 0,   "type": "CameraMove", "to": [12, 5], "seconds": 0 },
    { "time": 0.5, "type": "Fade", "to": 0, "seconds": 1 },
    { "time": 1,   "type": "MoveEntity", "entity": "name:Guard", "to": [14, 5], "seconds": 3, "wait": true },
    { "time": 4,   "type": "StartDialogue", "entity": "name:Guard", "wait": true },
    { "time": 4.5, "type": "SetVariable", "name": "intro_seen", "value": true },
    { "time": 5,   "type": "CameraRelease" } ] }
```

- Cues happen in order of `time` (a cue without one is at 0; ties keep the file's order).
- `"wait": true` holds the clock until that cue has finished, so what follows happens after it. A
  `Wait` or `WaitForEvent` always holds. Without it a fade, a camera move or a walk runs alongside
  the next cues.
- The cues the player does itself: `Wait {seconds}`, `WaitForEvent {event, source, other, data,
  timeout}`, `Fade {to, seconds, ease}`, `CameraMove {to | entity, seconds, height, ease}`,
  `CameraRelease`, `MoveEntity {entity, to | toEntity, seconds | speed, ease}`. Eases are `linear`,
  `smooth`, `in` and `out`. A `CameraMove` to an `entity` keeps looking at it afterwards; `height` is
  how many world units are seen vertically.
- **Any other type is a rule action** (`SetVariable`, `EmitEvent`, `PlaySound`, `EnableEntity`,
  `TriggerAnimation`, `PlayAnimation`, `GiveItem`, `StartQuest`, `StartDialogue`...). With `wait`, a
  `StartDialogue` holds the sequence until the conversation is closed. `time` and `wait` are the only
  keys a cue reserves; every other key belongs to the cue.
- A `SequencePlayer` component on an entity plays the file (`PlaySequence` from any rule, or
  `playOnStart` for an intro). It holds an input lock while it plays (unless `"lockInput": false`),
  raises `sequence.started` and `sequence.finished` (data: `sequence`, `skipped`, and `stopped` when
  it was cut short), and can be skipped by a named input action (`skipAction`; the file can forbid it
  with `"skippable": false`). Skipping runs what has not happened yet in its end state; stopping
  leaves things where they stand and gives the camera, the input and the screen back.
- A cue that names an entity that is not there is logged and passed over, never waited for.

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
