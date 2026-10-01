# Architecture

YK Engine is a C++20 2D engine with an editor. **The engine is the product; a game is data plus,
when it needs them, its own components.** The demo game in [`YK-DemoGame/`](../YK-DemoGame) is a
test of the engine: a complete two-player puzzle platformer built from the engine's parts, with no
code of its own and no line of the engine that knows it exists. The second data-only test,
[`YK-ExplorationDemo/`](../YK-ExplorationDemo), exercises top-down exploration; see
[the exploration pass](EXPLORATION.md).

## Repository map

```text
include/yk/  src/         the engine and the gameplay library (public headers / implementation)
editor/core/              what the editor does: documents, undo, selection, gizmos, projects (no window)
editor/ui/                Dear ImGui panels, the workbench, the scripted UI driver
player/                   yk_player: runs a project as a game (yk::host::runPlayer)
tools/yk/                 yk: validate, format, info, components, export, targets (yk::host::runTool)
YK-DemoGame/              the demo game: a project folder (data) and the tools that generate its art
YK-ExplorationDemo/       the top-down exploration test: two maps, dialogue and persistent gate
packaging/                macOS: Info.plist template, entitlements, the script that assembles the .app; icons
tests/                    unit, integration, UI scripts, install check, macOS bundle and diagnostics checks
docs/  LICENSES/  cmake/  scripts/  third_party/
```

## Modules and dependency direction

```text
   yk_player ---------+
   yk_editor (ui) ----+---->  yk::gameplay     plates, levers, doors, hazards, controller, level rules
   yk (command line) -+              |
   yk_editor_core ----+              v
   game modules (yours)         yk::engine
   (registered by the hosts)      core -> scene/reflection -> components -> assets/project
                                  input -> animation -> runtime (GameRuntime) -> physics (Box2D, private)
                                  graphics/platform/audio (SDL3, private)
```

- **`yk::engine`** knows no game, no editor and no particular gameplay. It holds the scene model
  and its reflection, the standard components, the input action system, animation, the headless
  runtime, physics, the renderer, audio, the asset and project formats, validation, export.
- **`yk::gameplay`** is a library of reusable building blocks on the engine. Nothing in it is
  specific to a game (the demo uses it as any other game would).
- **Game modules** are the games' own libraries: extra components registered on top of the two
  above. The demo has none. A game that needs C++ builds its own copies of the player and the
  editor with its components registered ([BUILDING.md](BUILDING.md#game-modules-custom-c-components)).
- **Hosts** (`yk_player`, `yk_editor`, `yk`) start from `registerStandardComponents`, which
  registers the engine's components and the gameplay library, and add the game module's. Each
  program is a function in `include/yk/host/Hosts.hpp` (`runPlayer`, `runEditor`, `runTool`,
  taking `argc`/`argv` and the game's registration function) behind a one-line `main`;
  `yk_add_game_hosts()` in `cmake/YkGame.cmake` writes the three mains for a game
  ([ADR 0011](decisions/0011-game-modules-are-hosted-by-libraries.md)).
- Nothing in `yk::engine` or `yk::gameplay` includes editor or game headers. Public headers
  contain no Box2D or SDL types. `YK_RUNTIME=OFF` builds everything that needs no window: the
  engine, gameplay, the editor core, the command line and their tests.

## Engine and game are separate, and so are editor and runtime

- **A game is a project folder** the engine reads. The engine's repository contains one as a
  sample and a test (`YK-DemoGame/`, selectable with `YK_DEMO_PROJECT`); nothing in `src/`,
  `include/` or `editor/` names it, and a game can live in a repository of its own (`yk new`, the
  editor's New Project, or a game module that pulls the engine in as a dependency).
- **The runtime is not the editor.** `yk_player` links `yk::gameplay`, the renderer and audio, and
  no editor code or Dear ImGui; an exported game is that program plus `data/`. The editor links
  the same runtime to play a copy of the scene in its own window. On macOS the engine application
  (`YK Engine.app`) carries the editor, the player (which it exports games with) and `yk`; an
  exported game carries the player only.
- **Where files come from** is asked of the operating system, never guessed: `yk/core/AppPaths.hpp`
  reports the running program's real path (`_NSGetExecutablePath`, `/proc/self/exe`,
  `GetModuleFileNameW`), the bundle's `Contents/Resources` when the program is in
  `X.app/Contents/MacOS`, and the per-user folders by each system's convention. Nothing depends on
  the working directory, on `argv[0]` or on the source tree at run time (`YK_SOURCE_DIR` is a
  test-only definition).

## Application lifecycle and diagnostics (`core/`)

A program started by double-click has no terminal, so everything a person would have read there is
kept on disk, and every failure has somewhere to go:

- **`Log`** writes every message to stderr and the in-app console as before, and now also to a
  rotated log file (`openLogFile`: `editor.log`, earlier runs `editor.1.log` ..., timestamped lines,
  flushed at once).
- **`DiagnosticsSession`** (RAII, one per program run) opens the log in the per-user log folder,
  installs the crash handler, ignores `SIGPIPE`/`SIGHUP` (closing the terminal a program was
  started from must not kill it) and writes a *running marker* (`<name>-<pid>.session`) that a
  clean exit removes. A marker left by a process that is no longer alive tells the next start
  that the last session did not end normally; the editor says so once and points at the crash
  report and the log folder.
- **The crash handler** writes `crash-<name>-<pid>-<time>.txt` (application, version, log file,
  signal, a stack trace) using only async-signal-safe calls, then re-raises the signal so the
  system's own report still happens. An uncaught C++ exception is logged with its message first.
  `--debug-crash segv|abort|throw` on the editor and the player crashes on purpose; the
  `diagnostics_crash` test uses it and reads the report.
- **`showFatalError`** (`platform/Application.hpp`) is the one way a start-up failure is reported:
  log, stderr, and a message box when a display exists. Both programs exit non-zero afterwards.
- **`Process`** (`runProcess`, `ToolRunner`) runs a child and captures its output (`posix_spawnp`).
  The exporter uses it for `codesign` and `hdiutil` through an injectable runner, which is how the
  unit tests check the exact commands without a Mac. The editor tracks the player it starts
  (`EditorState::startPlayerProcess`): polled every frame, ended by *Stop Player*, replaced by a
  second run, and terminated when the editor quits.

Logs and crash reports: macOS `~/Library/Logs/<org>/<app>`, Linux `$XDG_STATE_HOME/<org>/<app>/logs`,
Windows `%LOCALAPPDATA%\<org>\<app>\Logs`; `YK_LOG_DIR` overrides (the tests use it so a test
never writes into a real home).

## macOS bundles (`packaging/macos/`, `assets/Export.cpp`)

`YK Engine.app` is assembled by one CMake script (`assemble-app.cmake`: plain file copying, so a test
runs it on any system): `Contents/MacOS/{yk_editor,yk_player,yk}`, `Contents/Resources/{AppIcon.icns,
YK-DemoGame, licenses, docs}` and an `Info.plist` from the template (bundle id, version, a document
type so a `.ykproj` opens in the editor). `scripts/package-macos.sh` signs it and writes the `.dmg`;
an exported game is the same layout with the game's player as its only program and its project in
`Contents/Resources/data`. `SDL_GetBasePath()` reports `Contents/Resources` inside a bundle, which is
why the editor finds the demo and the exporter finds the player and the notices through
`../Resources` candidates as well as the build tree's and an installation's layouts. The `macos_bundle`
test builds a bundle in a foreign folder on any system and uses it (exports, runs the exported
bundle); `scripts/verify-macos-app.sh` does the same for the signed result on a real Mac.

## Units and conventions

1 world unit = 1 meter; +X right, +Y down. Angles are degrees in transforms and sprites, radians in
physics. The runtime advances in fixed 60 Hz ticks with bounded catch-up. Entity ids are random
64-bit values (hex in files) so independently edited scenes merge without renumbering. Paths in
data are project-relative with `/`.

## Scenes, components and reflection (`scene/`, `components/`)

A `Scene` owns a tree of `Entity` objects (id, name, active flag, tags, local `Transform2D`, parent
and ordered children, components, plus editor hints: `locked`, `editorHidden`, and `prefabSource`
on the root of a prefab instance). A scene is plain data: it can be built, edited, cloned and
saved with no running game.

Components are ordinary classes with public members. Each registers in an explicitly owned
`ComponentRegistry` (`registry.add<T>("Name")` plus `T::describe`), declaring every editable field
once with `field("name", &T::member)` and optional flags: range, enum options, asset kind,
`size`/`offset` (resize gizmo), `layer`, `displacement` (a world-space shift the editor previews),
`pin` (a point in the entity's own space the editor draws and lets you drag: a hinge's anchor),
input set and action pickers, `multiline`, `readOnly`. Type-level settings: category, description,
`dependsOn` (added automatically first), `onAdd` defaults (never applied when loading),
`allowMultiple`, `screenSpace`.

That one declaration drives all of: JSON serialization, the Inspector's widgets, prefab
instantiation and id remapping, undo (the editor snapshots serialized scenes), copy/paste, the
generated [components.md](components.md), project validation and the Components view of the
editor. Adding a component or a field means writing it once; no editor code changes, no
serialization code changes.

Loading validates structure and names the entity, component and property that is wrong; it never
returns half a scene. Prefab instantiation gives every entity a fresh id and remaps references
between the copies; references to entities outside the subtree are cleared. `reapplyPrefab`
rebuilds an instance from its prefab while keeping the root's identity, name, transform and place,
so links from other entities stay valid and wiring to outside entities survives.

## Projects, assets and export (`assets/`)

A project is a folder with `project.ykproj` (settings, layers, input map, build settings), scenes,
prefabs and assets ([PROJECT_FORMAT.md](PROJECT_FORMAT.md)). `AssetSource` is the runtime's read
interface (a project on disk, memory for tests), so the runtime never touches the file system by
name. Texture import settings live in `<texture>.ykmeta` sidecars merged with project defaults.
`validateProject` loads everything and reports what a game would trip over.

`exportGame` (`yk/assets/Export.hpp`) is the packaging step and the only one: the editor's
Export dialog and `yk export` call it. It validates the project, copies the player for the target
system and the project's data (development folders never ship), writes the README, notices,
`yk-export.json` and (macOS) `Info.plist`, validates the copy as a project of its own and removes
everything if that fails. `Zip.hpp` writes reproducible stored archives. `BuildTarget`
(Windows, macOS, Linux) names the layouts; the player program for a target is found next to the
tool, in `templates/<target>/`, or given explicitly, because it is compiled for that system and
cannot be produced by copying.

## Input (`input/`)

`Input.hpp` holds raw keyboard and gamepad state (`InputFrame`, four gamepad slots, deadzones).
`InputMap` is the data half: named **action sets** (a person, a seat) with named **actions**, each
with any number of bindings (`Key:W`, `Pad:South`, `PadAxis:LeftX-`), stored in the project.
`ActionInput` evaluates the map once per fixed tick and gameplay asks for `state("Player1",
"Jump")` or `axis("Player1", "MoveLeft", "MoveRight")`; asking for an unknown action reads as "not
pressed". Edges last for exactly the update that sees them, aliases never produce false releases,
and a tap inside one poll still registers. `PlayerInput` (a component) names a set, so two
characters with different keys need no code.

## Animation (`animation/`)

`AnimationSet` (`.ykanim`) is a sprite-sheet layout with clips: frame lists or ranges, fps or
per-frame durations, looping, `next`, and events (a footstep sound on frame 1). `Animator` plays
clips; `AnimationController` (`.ykctl`) is a state machine over the clips driven by generic
parameters (float, bool, trigger) with conditions, any-state transitions and exit times;
`AnimationPlayer` runs a set through a controller. The `AnimatedSprite` component wires them to a
sprite and mirrors it from a flip parameter. Gameplay publishes generic parameters (`speed`,
`grounded`, `facing`, `jumped`, `landed`, `dead`, `pressed`, `on`, ...) and never names a clip, so
art is replaced by editing data. Time advances only through the fixed tick, so animation is
deterministic.

## Runtime (`runtime/`)

`GameRuntime` executes a scene headlessly, so the standalone player, the editor's Play mode and
the tests run the same code. Given a scene it builds a Box2D world from `RigidBody`/`Collider`
components (colliders with no body become static geometry; colliders on descendants join their
nearest body), then per frame accumulates time into fixed ticks: input actions are evaluated,
component fixed updates run, the physics steps, transforms are written back, trigger overlaps and
collision callbacks fire, events are dispatched; then one variable update and late update. A frame
time within 0.4 ms of one, two or three ticks (or half of one, a 120 Hz screen) counts as exactly
that, so a display's slightly uneven frames do not turn into a tick less on one frame and two on
the next.
Deferred `destroyLater`, prefab `spawn`, `restart()` (rebuild from the start snapshot),
scene-change requests, a `Blackboard` of named variables (`{name}` placeholders in UI text) and a
bounded `EventBus` complete the `GameContext` components see. Collision layers are stored by name
and resolved through the project's `LayerConfig`, whose symmetric matrix covers solid collisions
and trigger overlaps alike.

**Transitions.** Restarts and scene changes go through a fade state machine the runtime owns (idle,
out, covered, in; `RuntimeOptions::transitionSeconds`, `screenFade()` for the renderer), and the
runtime keeps named **input locks** (`lockInput`, honored by `PlayerInput`, so the controls stop
while a level ends and the raw input is untouched). `Blackboard::keep` marks variables that survive
into the next scene. **`GameSession`** (`runtime/GameSession.hpp`) is the one place scenes are
switched: it owns the current `GameRuntime`, loads the scene a component asked for, carries the kept
variables over, starts it covered so it fades in, and stays on the current scene (logging why) when
the load fails. It also handles the pause action (`RuntimeOptions::pauseSet/pauseAction`, Global/Pause
by default), which a paused runtime could not hear itself; the player dims the picture and says
PAUSED (`GameViewOptions::paused`). The standalone player and the editor's Play mode both drive a session, so they cannot
disagree ([ADR 0015](decisions/0015-level-flow-and-transitions-belong-to-the-runtime.md)).

### Update order and services

Each fixed tick runs in this order ([ADR 0017](decisions/0017-services-and-update-phases.md)):

1. input actions are evaluated; components that were added are started (`onStart`); `scene_started`
   is raised once;
2. the event bus is told the time and delayed events whose time has come are queued;
3. **before the physics step**, for each phase in order, the *services* of the phase tick and then
   the components of that phase run their `onFixedUpdate`: `Clock` (world time, calendars),
   `PreUpdate` (scripts, rules, stat regeneration, status effects, health recovery),
   `Decision` (AI and schedule agents choose goals), `Gameplay` (the default phase: every
   component written before phases existed, so their behaviour is unchanged), `Steering`
   (navigation agents, avoidance), `Motor` (character motors turn intents into velocities);
4. tilemaps that changed rebuild their static colliders; the physics world steps; transforms are
   written back; trigger overlaps and collision callbacks fire;
5. **after it**: `Perception` (sight, hearing, awareness) and `PostSimulation` (the spatial index,
   inventories collecting what lies near, pickups, loot, crafting stations);
6. entities queued with `destroyLater` are removed, the event queue is dispatched, the tick count
   advances and the interpolation snapshot is taken.

A frame then runs one variable update and late update. Services are created on first use
(`context.services().get<T>()`) and belong to one run of one scene (`RuleService`,
`RandomService`, `DataService`, `SpatialIndexService`, `LootService`, `NavigationService`).

### The world model (`world/`, `navigation/`)

One scene is one continuous simulation with several **levels** (floors, roof, vents, underground):
`SceneSettings::levels`, `WorldLayer`, `levelOf(entity)`, per-level physics filtering and
per-level queries ([ADR 0016](decisions/0016-one-world-many-floors.md)). `SpatialHash` and the
`SpatialIndexService` answer "what is near here" without scanning; `Tileset`/`Tilemap` hold large
maps as chunked sparse layers with merged static colliders ([ADR 0022](decisions/0022-tilemaps-are-a-component-with-chunked-layers.md));
`WorldGrid` gives navigation, line of sight and sound transmission one view of the same tiles; and
`NavigationWorld` plans paths over it with budgets ([ADR 0020](decisions/0020-navigation-on-a-multi-level-grid.md)).

### Rules, stats, items and definitions (`rules/`, `stats/`, `items/`, `data/`)

`RuleCatalog` (an extension slot of the registry) holds the conditions, actions and fact namespaces
every module contributes; `RuleSet` runs WHEN/IF/THEN rules, and the same language is used inside item
uses, effects, recipes and container permissions ([ADR 0018](decisions/0018-one-condition-and-action-language.md)).
`GameData` loads a project's definition files (stats, status effects, items, loot tables and pools,
recipes, free tables) with per-file problem reporting ([ADR 0021](decisions/0021-definitions-are-versioned-data-in-gamedata.md));
`StatSet`, `StatusEffects` and `Health` give characters numbers, conditions and damage;
`Inventory`, `Container`, `Pickup`, `Crafter` and `CraftingStation` give them things. Components with
run-time state of their own implement `Component::saveState/loadState` (the save game contract).

### Characters (`gameplay/Character.hpp`, `items/Appearance.hpp`)

One `CharacterMotor` per character turns an intent into movement; the player's
`PlayerCharacterController`, a navigation agent, a cutscene or a script only set the intent. The motor
reads the character's status effects (`no_move`, `no_sprint`, `move.speed`) and spends stamina to
run ([ADR 0024](decisions/0024-one-character-motor-effects-as-flags-and-equipment-as-layers.md)).
`AppearanceLayers` draws what a character wears as child sprites that copy the body's frame.

### Quests, conversations and cutscenes (`sim/`)

Story is data on the same rule language ([ADR 0023](decisions/0023-quests-conversations-and-cutscenes-are-data-on-the-rule-language.md)).
`QuestDefinition`s live in `GameData`; a `QuestLog` component keeps one character's (or the world's)
quests, completes objectives from conditions, counted events and rules, and raises `quest.*` events.
`DialogueGraph` parses `.ykdialogue` (pages or nodes with choices, branches and actions) and
`DialogueSession` walks it for the `Dialogue` component, which `SceneRenderer` draws. `SequenceDefinition`
parses `.ykseq` timelines and `SequencePlayer` plays them: cues at times, `wait` to hold the clock,
rule actions as cues, `Camera::hold/release`, the cinematic fade (`GameContext::setCinematicFade`),
an input lock, skip and stop. All three hand their conditions and actions to the validator through
`RuleSourceVisitor`.

## Gameplay library (`gameplay/`)

Sources (`PressurePlate`, `Lever`, `Goal`, `TriggerZone`, `EventAction`) list `targets`; receivers
(`Door`, `MovingPlatform`) combine every source that reported to them (any/all, invert). Sources
report every tick, so a receiver never misses a change and never depends on update order. Levers
can fire on touch or on an interact action.

**Mechanisms are physical.** A `PressurePlate` is a kinematic pad with a solid collider: characters
and crates land on it, it senses its load from its own contacts, sinks under it carrying it down,
stops at an exact depth and rises when the load leaves; a separate trigger region is available when
an activation zone is wanted instead ([ADR 0013](decisions/0013-pressure-plates-are-solid-pads-that-carry-their-load.md)).
`Door` slides and/or turns about its hinge, `MovingPlatform` shuttles and/or spins, both by velocity
with easing, and both stop when something is squeezed in their way. `PlatformerController` moves a
dynamic capsule (a bullet, for continuous collision against kinematic pads) by velocity:
acceleration, variable-height jump, coyote time, jump buffering, slopes with ground snapping, and
riding whatever it stands on by matching the velocity of the ground *point* under its feet, so
sliding, rotating, tilting and sinking surfaces all carry it
([ADR 0014](decisions/0014-characters-ride-the-velocity-of-the-ground-point.md)); it reads its
actions from `PlayerInput`.

**Flow.** `LevelFlow` is the level's state machine (intro, playing, complete, failed) with goals, a
time limit, retry, continue, the next scene and the variables to carry over; it publishes
`level_state`, `level_message`, `level_time` and `level_time_left` to the Blackboard and raises
`level_started`, `level_completed` and `level_failed`. `Goal` is an exit: on completion whoever
stands in it walks in and fades. `EventAction` connects events to consequences with a delay or a
timer (a signal receivers follow, another event, entities switched on and off, animation triggers,
a variable, a sound, a restart, a scene change). `Killable` takes a character out of the world,
plays a death animation and returns it to its spawn point or last checkpoint. `Hazard` kills
entities matching `affectsTags` (empty: anything killable), which is how one pool is deadly to one
character and safe for another. `Collectible`, `Checkpoint` and `SpawnPoint` complete the set.
Effects live in the engine: `ParticleEmitter`, `Light2D` (an additive glow), `Oscillator`,
`Lifetime`. Components can also say what is wrong with them (`TypeBuilder::check`): a plate with
nothing to press, a hinge on a body that cannot swing. Project validation and the editor's Problems
panel show those sentences.

## Physics (`physics/`)

Ownership: `World` owns the native world and all bodies, shapes and joints; it is noncopyable.
Engine handles carry a weak identity token and a monotonically increasing 64-bit serial, so a
native generation wrap cannot resurrect a handle. No native id appears in the public API.
Destroying a body invalidates its shapes and joints. World calls stay on the creation thread
(asserted). Box2D provides the broad phase, contact solver, CCD and sleeping; engine code
validates input, adapts handles and coordinates, controls timing and copies backend events into
engine-owned values. `advance()` accumulates elapsed time into fixed ticks, clamps frame delays,
caps catch-up and reports dropped time; contact and sensor arrays are copied after every tick with
historical handles retained through destruction; nothing calls back from inside the solver. See
[physics.md](physics.md) for the API contract.

At the component level `Collider` offers box, circle, capsule and **wedge** (a right-triangle
ramp) shapes, **one-way** platforms (blocking only what comes down onto their top side), triggers
(overlap events only) and per-layer filtering. `cornerRadius` rounds a box (a stepping edge that does
not catch a foot) and `chamfer` cuts its corners (the rim of a plate); kinematic bodies carry what
stands on them, at the velocity of the point under each rider (the world reports it:
`World::pointVelocity`). `HingeJoint` pins a body to a point in the world or to another body so it
swings: seesaws, bridges, doors that give way, with limits, a spring and a motor; the anchor is a
pin the editor draws and lets you drag. Continuous collision for a body that must not tunnel through
kinematic mechanisms is a per-body flag (`World::setBullet`). Collision callbacks include
`onCollisionExit`, so a mechanism knows when a load leaves.

## Graphics, application and audio (`graphics/`, `platform/`, `audio/`)

`Application` owns SDL, the window and the renderer and runs an `ApplicationLayer` synchronously
on the main thread: input edges reset, platform events (offered to the layer first, so tools
consume their own input), frame clock, `update`, `beginFrame`, `render`, present. A run has either
a fixed letterboxed logical resolution (games) or native resolution (tools).

`Renderer` owns textures behind validated handles. Sprites sort by layer, then order, then
submission sequence. A frame may hold several **passes** with their own camera, viewport and
clear color, and a pass may target a **render-target texture**, which is how the editor renders
its scene and game views into panels. `SceneRenderer` turns scene data into submissions: sprites
with shapes, tint, sheet frame, **tiled** and **nine-slice** draw modes, alpha and additive
blending, **parallax** layers, **view culling**; particles and glows; screen-space UI text,
panels and images; collider outlines. The `Camera` component follows or frames its targets
(`Fixed`, `Follow`, `FitTargets`) with smoothing, padding, zoom limits and world bounds, one step per
fixed tick: what it follows moves in ticks, and a camera that glided at the display's rate (120 or
144 Hz) would slip against it. Nothing is interpolated between ticks, so on a display faster than
60 Hz the picture changes at 60 Hz (see [STATUS.md](STATUS.md#known-limitations)).
`drawGameView` shows a running game as a player sees it, `drawScenePreview` the same for a scene
that is not running. `SdlAudio` mixes procedural or `.wav` sounds behind the `AudioSink`
interface; without a device it plays nothing and never fails the game.

## The editor

### Editor core (`editor/core`, no SDL, no ImGui)

- **`EditorDocument`** holds the open scene, the selection, the undo history and the saved/dirty
  state. All changes happen inside a *change*: `beginChange` remembers the serialized scene,
  `endChange` compares and records one undo step if anything differs. A drag or a focused text box
  is one long change; a change that ends where it began leaves no step. Undo and redo rebuild the
  scene from snapshots (200 steps), so code between frames keeps `EntityId`s, never `Entity*`.
  Ready-made edits: create from a template or prefab, duplicate, delete, reparent, reorder,
  rename, activate, lock and hide, add and remove components (dependencies respected), set a
  property (validated and clamped like any load), copy/paste as portable JSON, prefab revert,
  update and unpack.
- **`EditorGeometry`** derives what to select and draw from reflection alone: extents from
  `size`/`offset` fields, picking (visible sprites topmost, then colliders and markers), rectangle
  selection, links and displacement ghosts; locked and hidden entities are skipped.
- **`SceneInteraction`** is the scene view's behavior as a state machine over pointer positions:
  click, ctrl/shift/alt selection, marquee, move with snapping and axis lock, resize handles,
  rotation, dragging a door's open target or a hinge's pin, nudging, and the view itself: panning,
  zooming about the pointer or the middle of the view (a ladder of round levels for the buttons and
  keys), fitting the scene, focusing the selection. It draws nothing, so it is unit tested without
  a window.
- **`EditorProject`** creates and opens projects, scenes and prefabs (paths validated to stay
  inside the project), imports assets, validates, exports, applies instances to prefabs and
  remembers recent projects. **`PlaySession`** copies the document through the save format into a
  `GameSession` (the same one the player uses, so level flow and scene changes behave identically);
  stopping drops the copy, so the edited scene is untouched.
- **`WorkbenchLayout`** is the layout arithmetic: which side bar view and panel tab are showing,
  the sizes, the split, and the rectangle of every region for a window size. The regions tile the
  window exactly and every stored size is clamped, so no part can collapse or push another out.
  It is tested without a window and saved as `workbench.json`.

### Editor UI (`editor/ui`)

Dear ImGui (docking branch, used as plain windows) with the SDL3 and SDL_Renderer backends, hosted
by `EditorApp`, an `ApplicationLayer`. The look is neutral and dark (`Theme.cpp`: colors, metrics,
the embedded Inter, JetBrains Mono and Codicons fonts; `UiCommon.cpp`: icons, pill tabs, buttons,
tooltips). `Workbench.cpp` draws the regions the layout computes as borderless windows pinned to
their rectangles (title bar with the menus and the Play/project capsule, activity bar and side bar
as one card, editor groups with tabs, the Inspector/Debug card, the panel, status bar, sashes), so
nothing floats or docks by accident. The layout arithmetic gives each card its rectangle (a gap
between neighbours, rounded corners); the regions are drawn as outlined cards on a darker canvas
with the background draw list, and their content is ordinary ImGui inside a transparent child.
Pixel details that cost time and are worth knowing: ImGui sizes fonts by line height, so icon
glyphs are centered on the line box and shifted by the text baseline (`drawIcon`); child windows
ignore padding unless asked (`AlwaysUseWindowPadding`); on macOS Dear ImGui swaps Control and
Command, so shortcut labels go through `shortcutText` and the scripted driver's `ctrl` means the
platform's shortcut key.
Anything that would change the open document while panels are still drawing it (a click on a tab)
is deferred until the frame ends. Panels are content-only functions: hierarchy, inspector (entity,
file and scene modes), scene view, game view, Explorer, Prefabs, Components, Build, console,
problems, build output, profiler, and modal dialogs. `EditorState` holds everything the editor
knows at run time and the actions that change it; panels read it and call the actions; the
actions draw nothing. Several scenes are open at once by swapping the active document with a
map of background ones, so the rest of the editor only ever deals with one.

Widgets that edit the scene do it through `commitEdit`, which opens one document change per
interaction. While playing, panels show the running copy read-only.

### Scripted UI driver (`EditorDriver`, `yk_editor --script`)

Widgets register their screen rectangles under stable names (`toolbar/Play`, `hierarchy/Gate`,
`activity/Explorer`, `dialog/Export/export`, ...). A script clicks, drags, types and presses keys
by pushing real SDL events, scrolls off-screen widgets into view, takes screenshots and checks
the editor's state. The CTest entries `editor_*` run scripts against the real UI under SDL's dummy
video driver and software renderer ([EDITOR.md](EDITOR.md#testing-the-editor)).

## Errors, validation and threading

Expected invalid input (malformed files, bad geometry, stale handles) returns `Result`/`Status`
errors; programmer errors assert in debug builds. Physics boundary values must be finite with
magnitude at most 1,000,000 and small geometry at least 0.005 m; those checks prevent solver
assertions and are not a claim that extreme scales simulate well. All runtime and editor code
runs on the main thread (the only other thread is whatever SDL uses for a native file dialog,
which hands its result over through a mutex-guarded queue); there is no job system.

## Rules of the codebase

**Boundaries.** Public headers in `include/yk/`, implementation in `src/`, editor logic in
`editor/core` (no SDL, no UI toolkit) and panels in `editor/ui`, tests in `tests/`. The engine
and gameplay libraries never include editor or game headers and contain no rules of any
particular game: game rules go in a game module, reusable mechanics in `yk::gameplay`, a
capability every game needs in the engine. No global service locators or mutable singleton
worlds; prefer values, RAII, unique ownership and explicit non-owning handles that reject
destroyed objects and foreign owners. Physics has no window, renderer or scene dependency and
Box2D stays private.

**Data.** Editable state is public component data declared once through the registry; serialization,
inspector widgets, prefabs, undo, copy/paste, generated docs and validation all read that
declaration, so no editor or file-format code is written for an individual component. Runtime-only
state is declared read-only (shown, never saved). Files reference entities by id and assets by
project-relative path; loaders validate and never return partial data. `docs/components.md` is
generated (`yk components`) and a test fails when it is stale.

**The editor.** Everything the editor does to a scene lives in `editor/core` and is unit tested
without a window; UI code calls it and draws, and never edits a scene directly. Every scene
modification is a change on an `EditorDocument`, so it is undoable and dirty tracking stays right.
Between frames hold ids, never pointers. Play mode runs a copy and never modifies the edited
document. Behavior a person can see or click needs a UI script in `tests/editor/`; new widgets a
script may need register a name with `markItem`. No fake controls: every menu item and button
does what it says or is disabled.

**Physics and time.** Meters, kilograms, seconds and radians in physics; world units and degrees
in rendering. Fixed simulation ticks are separate from frame time. No callback mutates a locked
solver world. Reject expected misuse through `Result`; assertions are for programmer invariants;
no swallowed failures; no raw owning `new`/`delete` outside tightly controlled private factories.
CCD and sensor limits are documented rather than hidden, and bitwise determinism across
platforms is not claimed (the demo's bot playthrough does reach the same result on Linux and on
Windows under Wine).

**Build and tests.** Warnings are errors. Dependencies are pinned by hash or commit and their
notices ship with anything redistributed ([THIRD_PARTY.md](../THIRD_PARTY.md)). Tests verify
behavior, lifecycle, invalid input and regressions; a failing test gets its cause fixed, not its
assertion weakened. Gameplay changes need a headless simulation, rendering changes need real
pixel or readback evidence, level content needs the bot playthrough, UI behavior needs a driver
script. Gates before a change is handed off: configure, build and CTest for `dev`; `release`,
`asan` and `headless` for shared code; format check; `git diff --check` (`scripts/verify.sh`);
and `scripts/verify-windows.sh` when platform code moved. What is installed is tested (`install`).

## Decisions

Durable choices are recorded as short ADRs in [decisions/](decisions/).
