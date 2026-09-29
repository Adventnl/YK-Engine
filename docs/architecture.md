# Architecture

## Modules and dependency direction

```text
                         +------------------ prototype game module (game/) ------------------+
                         |  LevelFlow, character/exit/pool builders; registerAllModules()      |
                         +---------------------------------+----------------------------------+
                                                           | depends on
   yk_player  ---------+                                   v
   yk_editor (ui) -----+---->  yk::gameplay   (plates, doors, hazards, controller, layers)
   yk_editor_core -----+                |
                                        v
                                  yk::engine
   scene/reflection -> components -> runtime (GameRuntime) -> physics (private Box2D)
   core (Json, Result, Log, files) <- everything            renderer/app/audio (private SDL3)
```

- **`yk::engine`** knows nothing about any game, the editor, or particular gameplay. It holds the
  scene model and its reflection, standard components (sprite, collider, camera, UI...), the
  headless runtime, physics, renderer, audio, assets and the project format.
- **`yk::gameplay`** is a library of reusable components built on the engine: signals, pressure
  plates, levers, doors, moving platforms, hazards, collectibles, checkpoints, goals, trigger zones,
  spawn points, a platformer controller, "killable" respawn, and the standard collision layers.
  Nothing in it is specific to one game.
- **The game module** (`yk_prototype_game`) holds only what is specific to *Elemental Prototype*:
  `LevelFlow` (win/lose rules, restart, messages) and entity templates for its characters, exits
  and pools. `registerAllModules` composes engine + gameplay + game into one `ComponentRegistry`.
  The editor and the player are *hosts*: each links the modules it is given (today, the prototype),
  so the editor edits exactly what the player can run.
- **The editor** is two libraries: `yk_editor_core` (no SDL, no UI toolkit: everything the editor
  *does*) and `yk_editor_ui` (Dear ImGui panels that call it), plus the `yk_editor` executable.
- Nothing in `yk::engine` or `yk::gameplay` may include editor or game headers. A new game adds its
  own module beside `game/`; a new engine capability goes in the engine only if no game logic is
  involved.

Public headers are in `include/yk/`, implementations in `src/`, and Box2D and SDL types never appear
in them. `YK_RUNTIME=OFF` builds everything that needs no window (engine, gameplay, game module,
editor core, all their tests).

## Units and conventions

1 world unit = 1 meter; +X right, +Y down. Angles are degrees in transforms and sprites, radians in
physics. The runtime advances in fixed 60 Hz ticks with bounded catch-up. Ids of entities are random
64-bit values (hex strings in files) so independently edited scenes merge without renumbering.

## Scenes, components and reflection (`scene/`, `components/`)

A `Scene` owns a tree of `Entity` objects (id, name, active flag, tags, local `Transform2D`, parent
and ordered children, components). A scene is plain data: it can be built, edited, cloned and saved
with no running game.

Components are ordinary classes with public members. Each class registers itself in an explicitly
owned `ComponentRegistry` (`registry.add<T>("Name")` plus `T::describe`), declaring every editable
field once with `field("name", &T::member)` and optional flags: range, enum options, asset kind,
`size`/`offset` (resize gizmo), `layer`, `displacement` (a world-space shift the editor previews),
`multiline`, `readOnly`. Type-level settings: category, description, `dependsOn` (components added
automatically first), `onAdd` defaults (never applied when loading), `allowMultiple`, `screenSpace`.

That one declaration drives **all** of: JSON serialization, the inspector's widgets, prefab
instantiation and id remapping, undo (the editor snapshots serialized scenes), copy/paste, the
generated `docs/components.md`, and project validation. Adding a component or a field means writing
it once; no editor code changes.

Files: `.ykscene` (scene), `.ykprefab` (an entity subtree), `project.ykproj`. Loading validates the
structure and names the entity/component/property that is wrong; it never returns half a scene.
Prefab instantiation gives every entity a fresh id and remaps references between the copies;
references to entities outside the subtree are cleared (duplicate/paste inside one scene keeps them,
and remaps references among the copied entities themselves).

## Projects and assets (`assets/`)

A project is a folder: `project.ykproj` (name, window, start scene, named collision layers with an
interaction matrix), scenes, prefabs and assets. Paths in data are project-relative with `/`
separators; `Project::resolve` rejects absolute paths and `..`. `AssetSource` is the runtime's read
interface (a project on disk, memory for tests). `validateProject` loads everything and reports what
a game would trip over: broken documents, references to missing entities, missing asset files,
unknown layers, a missing start scene. Sounds may be procedural (`tone:hz,seconds[,wave]`) and
sprites procedural shapes, so placeholder content needs no files.

## Runtime (`runtime/`)

`GameRuntime` executes a scene headlessly, so the standalone player, the editor's Play mode and the
tests all run the same code. Given a scene it builds a Box2D world from `RigidBody`/`Collider`
components (colliders with no body become static geometry; colliders on descendants join their
nearest body), then per frame accumulates time into fixed ticks: component fixed updates, physics
step, transform write-back, trigger overlaps and collision callbacks, events; then one variable
update and late update. Key edges are re-timed to exactly one tick. Deferred `destroyLater`, prefab
`spawn`, `restart()` (rebuild from the start snapshot), scene-change requests, a `Blackboard` of
named numeric variables (`{name}` placeholders in UI text) and a bounded `EventBus` complete the
`GameContext` components see. Collision layers are stored by name in components and resolved through
the project's `LayerConfig`, whose symmetric matrix covers both solid collisions and trigger
overlaps. Triggers are polled each tick; a trigger sees static shapes and other triggers only when
both layers accept each other.

## Gameplay library (`gameplay/`)

Sources (`PressurePlate`, `Lever`, `Goal`, `TriggerZone`) list `targets`; receivers (`Door`,
`MovingPlatform`) combine every source that reported to them (any/all, invert). Sources report every
tick, so a receiver never misses a change and never depends on update order.
`PlatformerController` moves a dynamic capsule by velocity (acceleration, variable-height jump,
coyote time, jump buffering, slopes, riding moving platforms) and reads its keys from properties, so
two characters with different keys need no code. `Killable` disables body, colliders and sprite
while dead and returns the entity to its spawn point or last checkpoint. `Hazard` kills entities
matching `affectsTags` (empty: anything killable), which is how one pool can be deadly to fire and
safe for water.

## Physics (`physics/`)

Ownership: `World` owns the native world and all bodies, shapes and joints; it is noncopyable and
nonmovable. Engine handles carry a weak identity token and a monotonically increasing 64-bit
serial; records are stored by serial, so native 16-bit generation wrap cannot resurrect a handle. No
native ID appears in the public API. Destroying a body removes attached shapes/joints and
invalidates their handles. Reverse shape lookup keeps identities until the next solver tick so end
events can refer to destroyed shapes. World calls stay on the creation thread (asserted).

Box2D provides the broad phase, contact solver, CCD and sleeping. Engine code validates input,
adapts handles and coordinates, controls timing and copies backend events into engine-owned values.

`advance()` accumulates elapsed time into fixed ticks, clamps frame delays, caps catch-up work and
reports dropped time. Poses are stored before each tick so `interpolatedPose` can blend previous and
current states (one tick behind); `state` is always authoritative. Contact and sensor arrays are
copied right after every tick, with historical handles retained through destruction; there are no
callbacks inside the solver. See `physics.md` for the API contract.

## Renderer, application and audio (`graphics/`, `platform/`, `audio/`)

`Application` owns SDL, the window and the renderer (destroyed in reverse order) and runs an
`ApplicationLayer` synchronously on the main thread: input edges reset, platform events (offered to
the layer first, so tools consume their own input), frame clock, `update`, `beginFrame`, `render`,
present. Closing the window is offered to the layer first (`onCloseRequested`), so a tool can ask
about unsaved work. A run has either a fixed letterboxed logical resolution (games) or native
resolution (tools: one unit = one pixel).

`Renderer` owns textures. Handles validate a weak renderer identity and an append-only index.
Sprites sort by layer, depth and submission sequence. A frame may hold several **passes**: each has
its own camera, viewport rectangle and clear color, and may target a **render-target texture**
instead of the frame, which is how the editor renders its scene and game views into panels.
Procedural textures (white, circle) and a built-in 5x7 bitmap font mean placeholder art needs no
files. `SceneRenderer` turns scene data into submissions (sprites with shape/tint/sheet frame,
screen-space UI panels and text, collider outlines from component data); `drawGameView` shows a
running game as a player sees it; `drawScenePreview` does the same for a scene that is not running.
`SdlAudio` mixes procedural or `.wav` sounds behind the `AudioSink` interface; without a device it
plays nothing and never fails the game.

## The editor

### Editor core (`editor/core`, no SDL, no ImGui)

- **`EditorDocument`** holds the open scene, the selection (last selected = primary), the undo
  history and the saved/dirty state. All changes happen inside a *change*: `beginChange` remembers
  the serialized scene, `endChange` compares and, if anything differs, records one undo step. A
  drag or a focused text box is one long change, so one Undo reverts it; a change that ends where it
  began leaves no step. Undo and redo rebuild the scene from the remembered snapshot (200 steps), so
  code between frames keeps `EntityId`s, never `Entity*`. Ready-made edits: create from a template or
  prefab (unique names), duplicate, delete (clearing references to deleted entities), reparent
  (keeping world position), reorder, rename, activate, add/remove components (dependencies respected),
  set a property (validated and clamped like any load), copy/paste as portable JSON.
- **`EditorGeometry`** derives what to select and draw from reflection alone: extents from
  `size`/`offset` fields (screen-space components excluded), picking (visible sprites topmost by
  layer/order, then colliders and markers smallest first), rectangle selection, links (entity
  references held by properties) and displacement ghosts.
- **`SceneInteraction`** is the scene view's behavior as a state machine over pointer positions:
  click, ctrl/shift/alt selection, marquee, move with grid snapping and axis lock, resize handles that
  scale sizes/offsets proportionally about the pivot (mirrored and rotated entities handled),
  rotation with angle snapping, dragging a door's open target, nudging, panning and zooming. It
  draws nothing, so it is unit tested without a window.
- **`EditorProject`** creates/opens projects, scenes and prefabs (paths validated to stay inside the
  project), lists assets, validates, exports the game (player next to a project copy) and remembers
  recent projects. **`PlaySession`** copies the document through the save format into a
  `GameRuntime`; stopping drops the copy, so the edited scene is untouched.
  **`ConsoleLog`** collects engine log messages for the console panel.

### Editor UI (`editor/ui`)

Dear ImGui (docking branch) with the SDL3 and SDL_Renderer backends, hosted by `EditorApp`, an
`ApplicationLayer`. Each frame builds all panels, then renders the Scene and Game views into render
targets (displayed with `ImGui::Image`) and draws the UI over them. Panels: hierarchy (tree,
drag-reparent, rename, filter, context menu), inspector (widgets generated from reflection: entity
fields with picker, eyedropper and drag-and-drop, entity lists, tag lists, asset fields, layer
combos, colors), scene view (grid, camera frame, links, gizmos, overlays), game view (camera
preview; the running game in Play), assets (prefab drag-and-drop), console, and modal dialogs.
Widgets that edit the scene do it through `commitEdit`, which opens one document change per
interaction. While playing, panels show the running copy read-only.

### Scripted UI driver (`EditorDriver`, `yk_editor --script`)

Widgets register their screen rectangles under stable names (`toolbar/Play`, `hierarchy/Door`,
`prop/targets/add`, ...). A script clicks, drags, types and presses keys by pushing real SDL events,
scrolls off-screen widgets into view, takes screenshots and checks the editor's state (`expect
selected`, `expect links`, `expect runtime-moved`...). The CTest entries `editor_workflow`,
`editor_sample_play` and `editor_sample_edit` run scripts against the real UI under SDL's dummy
video driver and software renderer.

## Errors, validation and threading

Expected invalid input (malformed files, bad geometry, stale handles) returns `Result`/`Status`
errors; programmer errors assert in debug builds. Allocation and standard-library exceptions may
propagate. Physics boundary values must be finite with magnitude at most 1,000,000 and small
geometry at least 0.005 m; those checks prevent solver assertions and are not a claim that extreme
scales simulate well. All runtime and editor code runs on the main thread; there is no job system.
