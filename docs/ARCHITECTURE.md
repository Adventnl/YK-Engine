# Architecture

YK Engine is a C++20 2D engine with an editor. **The engine is the product; a game is data plus,
when it needs them, its own components.** The demo game in [`YK-DemoGame/`](../YK-DemoGame) is a
test of the engine: a complete two-player puzzle platformer built from the engine's parts, with no
code of its own and no line of the engine that knows it exists.

## Repository map

```text
include/yk/  src/         the engine and the gameplay library (public headers / implementation)
editor/core/              what the editor does: documents, undo, selection, gizmos, projects (no window)
editor/ui/                Dear ImGui panels, the workbench, the scripted UI driver
player/                   yk_player: runs a project as a game
tools/yk/                 yk: validate, format, info, components, export, targets
YK-DemoGame/              the demo game: a project folder (data) and the tools that generate its art
tests/                    unit, integration, UI scripts, install check
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
  registers the engine's components and the gameplay library, and add the game module's.
- Nothing in `yk::engine` or `yk::gameplay` includes editor or game headers. Public headers
  contain no Box2D or SDL types. `YK_RUNTIME=OFF` builds everything that needs no window: the
  engine, gameplay, the editor core, the command line and their tests.

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
collision callbacks fire, events are dispatched; then one variable update and late update.
Deferred `destroyLater`, prefab `spawn`, `restart()` (rebuild from the start snapshot),
scene-change requests, a `Blackboard` of named variables (`{name}` placeholders in UI text) and a
bounded `EventBus` complete the `GameContext` components see. Collision layers are stored by name
and resolved through the project's `LayerConfig`, whose symmetric matrix covers solid collisions
and trigger overlaps alike.

## Gameplay library (`gameplay/`)

Sources (`PressurePlate`, `Lever`, `Goal`, `TriggerZone`) list `targets`; receivers (`Door`,
`MovingPlatform`) combine every source that reported to them (any/all, invert). Sources report
every tick, so a receiver never misses a change and never depends on update order. Levers can
fire on touch or on an interact action. `PlatformerController` moves a dynamic capsule by
velocity: acceleration, variable-height jump, coyote time, jump buffering, slopes with ground
snapping, riding moving platforms; it reads its actions from `PlayerInput`. `Killable` takes a
character out of the world, plays a death animation and returns it to its spawn point or last
checkpoint. `Hazard` kills entities matching `affectsTags` (empty: anything killable), which is
how one pool is deadly to one character and safe for another. `Collectible`, `Checkpoint`,
`SpawnPoint`, `Goal` and `LevelFlow` (win/lose rules, restart action, next scene, `level_state`,
`level_message` and `level_time` published to the Blackboard) complete the set. Effects live in
the engine: `ParticleEmitter`, `Light2D` (an additive glow), `Oscillator`, `Lifetime`.

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
(overlap events only) and per-layer filtering; kinematic bodies carry what stands on them.

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
(`Fixed`, `Follow`, `FitTargets`) with smoothing, padding, zoom limits and world bounds.
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
  rotation, dragging a door's open target, nudging, panning, zooming, framing. It draws nothing,
  so it is unit tested without a window.
- **`EditorProject`** creates and opens projects, scenes and prefabs (paths validated to stay
  inside the project), imports assets, validates, exports, applies instances to prefabs and
  remembers recent projects. **`PlaySession`** copies the document through the save format into a
  `GameRuntime`; stopping drops the copy, so the edited scene is untouched.
- **`WorkbenchLayout`** is the layout arithmetic: which side bar view and panel tab are showing,
  the sizes, the split, and the rectangle of every region for a window size. The regions tile the
  window exactly and every stored size is clamped, so no part can collapse or push another out.
  It is tested without a window and saved as `workbench.json`.

### Editor UI (`editor/ui`)

Dear ImGui (docking branch, used as plain windows) with the SDL3 and SDL_Renderer backends, hosted
by `EditorApp`, an `ApplicationLayer`. The look is neutral and dark (`Theme.cpp`: colors, metrics,
the embedded Inter, JetBrains Mono and Codicons fonts; `UiCommon.cpp`: icons, buttons,
tooltips). `Workbench.cpp` draws the regions the layout computes as borderless windows pinned to
their rectangles (title bar with the menus and Play controls, activity bar, side bar, editor
groups with tabs, inspector, panel, status bar, sashes), so nothing floats or docks by accident.
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
