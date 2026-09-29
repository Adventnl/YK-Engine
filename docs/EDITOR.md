# Editor guide

`yk_editor` is where levels are built. It edits the same scene files the player runs, and Play runs
the scene inside the editor with the same runtime the player uses.

```sh
yk_editor                           # welcome screen: New Project, Open Project, the demo game
yk_editor path/to/project           # open a project (a folder, or its project.ykproj)
yk_editor path/to/project --scene scenes/level2.ykscene
yk_editor --size 1920x1080          # window size
```

A project path that cannot be opened is reported in a dialog and the welcome screen stays.

The welcome screen offers **Open the Demo Game** (Cinder Vale, in `YK-DemoGame/`): open it, press
**F5**, then select the lever and see the arrow to the gate it opens. The demo is a normal project;
nothing in the editor knows about it.

## The workbench

The window is laid out like a code editor's:

```text
+--------------------------------------------------------------------------------------------+
| YK  File Edit View Scene Entity Component Build Debug Help    [> || >| [] <-]   [_ = _]     |  title bar
+----+---------------------+-------------------------------------------+--------------------+
|    | SCENE               | level01.ykscene *  |  Game                 | INSPECTOR          |
| A  |  Camera             +-------------------------------------------+                    |
| c  |  Backdrop           |  move resize rotate | snap 0.5 m | ...    | (an entity, a file |
| t  |   Far Wall          |  Cinder Vale > Terrain > Ground            |  or the scene)     |
| i  |  Terrain            |                                            |                    |
| v  |   Ground Start      |            scene view / game view          |                    |
| i  |   ...               |                                            |                    |
| t  |                     +-------------------------------------------+                    |
| y  |                     | CONSOLE  PROBLEMS  BUILD OUTPUT  PROFILER |                    |
|    |                     |  [info] Opened scene scenes/level01.ykscene|                    |
+----+---------------------+-------------------------------------------+--------------------+
| Cinder Vale   level01.ykscene *   0 0                      171 entities  Snap 0.50 m  60 fps |  status bar
+--------------------------------------------------------------------------------------------+
```

- **Title bar**: the menus, the **Play / Pause / Step / Stop / Restart** controls in the middle, and
  buttons on the right that show or hide the side bar, the panel and the inspector.
- **Activity bar** (far left): one icon per side bar view. Click the visible one again to hide the
  side bar.
- **Side bar**: **Explorer** (project files), **Scene** (the entity tree), **Prefabs**,
  **Components** (a catalog of every component with its description) and **Build** (validate, run,
  export).
- **Editor area**: tabs. Every open scene has a tab (a dot marks unsaved changes) and the **Game**
  view has one. *View > Editor Layout > Split Right / Split Down* puts the Game view beside or
  below the scene so you can edit and watch at once; close its tab to unsplit.
- **Inspector**: shows the selected entity, or the file picked in the Explorer, or (when nothing is
  selected) the scene's own settings.
- **Panel**: **Console** (everything the engine and editor log, filterable by level and text),
  **Problems** (the last project check), **Build Output** (export and run messages) and
  **Profiler** (frame times, draw statistics, physics numbers while playing).
- **Status bar**: project and scene, the problem count (click to open Problems), a running Play
  is shown in orange, entity and selection counts, snapping, zoom and frame rate.

Every divider is draggable. Sizes, the visible views, the split and which panel tab is open are
remembered between runs (`workbench.json`); **View > Reset Layout** restores the default.

## Projects, scenes, prefabs

- **File > New Project** asks for a name and a folder and creates the project, the standard
  collision layers (Default, Solid, Player, Sensor, Prop), the standard input sets and a first
  scene with a camera. **Open Project**, **Recent Projects**, **New Scene**, **Open Scene**,
  **Save Scene** (Ctrl+S), **Save Scene As**, **Save All**, **Close Scene**.
- **Scenes open in tabs.** Switching tabs keeps each scene's unsaved changes, undo history and
  scene view position. Closing a tab, closing the project or quitting with unsaved changes asks
  first (Save, Don't Save, Cancel).
- **Entity > Save as Prefab** stores the selected entity and everything under it in `prefabs/`.
  Drag a prefab from the Prefabs or Explorer view into the scene (or double-click it) to place a
  copy. See [Prefab instances](#prefab-instances).
- **File > Project Settings** edits the project: **General** (name, window, start scene),
  **Collision Layers** (which layers touch, for solid collisions and triggers alike), **Input**
  (the action sets and their bindings, below), **Rendering** (texture defaults) and **Build**
  (product name, executable, version, bundle id, files left out of an export).

### The input map

Gameplay asks for actions by name; the map says which inputs drive them. Each **action set** is one
controller (Player1: WASD and pad 1; Player2: arrows and pad 2; Global: restart, pause). Pick a set
on the left; on the right rename it, choose which gamepad it also listens to, add or remove
actions, and add bindings with the **+** on each action: **Listen for a key...** (press the key),
or pick a key, a gamepad button or an axis half from the menus. Click a binding to remove it.
Components use the names: a `PlayerInput` component picks the set, `Lever.interactAction` an
action. The map is validated when you press Save.

## Working in the scene view

| Do this | To |
|---|---|
| Click / Ctrl+click / Shift+click | select / toggle / add to the selection |
| Alt+click | select the next entity underneath |
| Drag empty space | box-select |
| Drag an entity | move it (all selected entities move together) |
| **W / R / E** or the toolbar | Move, Resize, Rotate tool |
| Resize tool: drag a handle | resize; Shift keeps proportions, Alt resizes from the center |
| Rotate tool: drag the knob | rotate (snaps to 15 degrees) |
| Drag the dashed ghost of a door or moving platform | set how far it moves |
| Arrow keys (Shift for more) | nudge |
| **F** / **Home** | frame the selection / everything you can edit |
| Mouse wheel | zoom at the cursor |
| Middle or right drag, or Space + left drag | pan |
| Right-click | create here, paste here |
| Hold **Ctrl** while dragging | toggle grid snapping (the toolbar magnet turns it on and off) |
| **Esc** | cancel a drag or a pending pick |
| Ctrl+Z, Ctrl+Y | undo, redo (every drag or typed edit is one step) |
| Ctrl+C, X, V, D, Delete | copy, cut, paste, duplicate, delete |

Resizing scales a sprite, its collider and other sized parts together, keeps the entity's pivot at
the same relative place in the box, and leaves children where they are. Duplicating or pasting a
plate together with its door keeps the copy of the plate opening the copy of the door. **Frame
All** ignores locked and hidden entities, so a huge locked backdrop does not push the level into a
corner.

The toolbar above the view has the tools, snapping and its grid size, and an **Overlays** menu
(grid, collider outlines, sprite bounds, pivots, links of the selection or all links, entity
names, the game camera's frame). The line below the toolbar is a breadcrumb of the selection's
place in the hierarchy; click a part to select it.

### Hierarchy

Click to select (Ctrl toggles, Shift selects a range), double-click to frame, drag a row onto
another to make it a child (onto empty space to move it to the top level), **F2** or the context
menu to rename, right-click for Create Child, Add Component, Duplicate, Delete, Move Up/Down, Save
as Prefab and Prefab actions. The search box filters by name or component. On the right end of a
row, the **eye** hides the entity in the editor only and the **lock** makes it unselectable and
immovable (the running game ignores both); use them on a backdrop or on parts you are finished
with. The **+** button creates entities from the built-in templates (Basic, Effects, Gameplay,
Level, Mechanisms, UI) or from your prefabs.

## The Inspector

- **An entity**: name, active flag, tags, then Transform and one section per component with a field
  per property. Everything is generated from the components' own declarations, so every component
  looks and behaves the same way. `...` on a section removes the component (refused while another
  component needs it) or resets it; the checkbox disables it; **Add Component** searches all
  components. With several entities selected the Inspector shows the one you clicked last; a change
  to a component's property is made on the same component of every selected entity that has one
  (a single undo step), while the name, tags and Transform fields change only the one shown (drag in
  the scene view to move them all).
- **Entity references** (a plate's targets, a camera's targets) take an entity three ways: the
  picker, the eyedropper (then click the entity in the scene view) or dragging a row from the
  hierarchy onto the field. A cyan arrow in the scene view shows every link of the selection.
- **The scene** (nothing selected): name, gravity and background color, plus a summary. Edits are
  undoable like any other.
- **A file** picked in the Explorer:
  - *textures* show a preview with the sprite-sheet grid and slice borders, and the **import
    settings** (pixels per unit, filter, sprite sheet grid, slice border). **Apply** writes the
    `.ykmeta` sidecar next to the picture (and removes it when every setting is off); **Revert**
    discards the edits;
  - *animation clips* play in a live preview (pick a clip, pause, restart) and their frame rate
    and looping can be edited and applied;
  - *animation controllers* list their parameters, states and transitions;
  - *sounds* show a waveform and can be played;
  - *scenes* and *prefabs* summarize themselves and offer Open, Set as Start Scene, Add to Scene.

  Selecting an entity again brings the entity view back.

## Assets

The **Explorer** lists the project's files as a folder tree with an icon per kind. Click a file to
inspect it, double-click a scene to open it or a prefab to add it, right-click for the context menu
(Open, Set as Start Scene, Add to Scene, Copy Path, Show in File Manager). The search box opens the
folders that contain matches.

To bring pictures and sounds in, use **Import** (the button above the tree) or drop files on the
window. PNG and BMP images and WAV sounds are copied into the project (`assets/textures`,
`assets/audio`, or the folder you have selected); a file is never overwritten, a numbered name is
used instead. Then pick the file from an asset field's folder button or drag it from the Explorer.
Until real art exists everything works with placeholder shapes: a sprite with no texture draws a
colored rectangle or ellipse, and sound fields accept placeholder tones (`tone:660,0.12`).

## Prefab instances

A prefab is a template that remembers where its copies came from. An instance is a full copy in
the scene; its root shows a **Prefab** bar in the Inspector and a Prefab submenu in the Entity menu
and the hierarchy's context menu:

- **Revert to Prefab** puts the instance back to what the prefab file says. Its name, position,
  rotation and size stay, links from other entities to it stay valid, and references it holds to
  entities outside the instance (a plate wired to a door) survive.
- **Apply to Prefab** writes the instance into the prefab file. Other instances keep their contents
  until you choose **Update Other Instances**, which asks first: instances carry no override data,
  so an update replaces their own changes (other than name and placement). Only the open scenes
  are updated.
- **Unpack Prefab** forgets the link; **Show Prefab in Explorer** finds the file.

## Play, Stop, and what stays yours

**F5** (or the Play button) starts a play session: the editor copies the open scene (including
edits you have not saved) into the runtime and shows it in the Game view, which takes the keyboard.
Every action set is live (Player1 and Player2 at once), mechanisms run, the camera frames as
authored. Pause (F6), Step (F10) and Restart work as the controls show. While playing, the
hierarchy and Inspector show the running copy read-only, and editing is disabled. **Shift+F5**
stops: the copy is thrown away and the editor is exactly as you left it, including what was
selected and the undo history. **Debug > Run in Player** (Ctrl+F5) starts the standalone player on
the project instead, as a separate process.

The Game view's buttons restart the scene and toggle physics shapes, collider outlines and
statistics. The game hears the keyboard while its view has focus (pressing Play gives it focus);
click elsewhere in the editor to type into the editor again.

## Validating and exporting

**Build > Validate Project** loads every scene and prefab and lists problems in the Problems panel:
files that do not load, references to missing entities, missing asset files, unknown layers, a
missing start scene, prefab links to files that are gone. Click a problem to go to it. The status
bar's problem count is refreshed after saves.

**Build > Export Game** packages the game (see
[Exported games](PROJECT_FORMAT.md#exported-games)): choose the target system, the folder, and
whether to also write a `.zip`. The export needs the player program built for that system: the
one next to the editor serves its own system; other systems need their `yk_player` built there
(or cross-compiled, [BUILDING.md](BUILDING.md)) and given in the *Player program* field or placed
in `templates/<system>/` beside the editor. The Build view lists which systems are ready. The
steps go to **Build Output**, and the finished dialog can show the folder.

## Menus

| Menu | What is in it |
|---|---|
| **File** | New/Open/Recent Project, New/Open/Save/Save As/Save All/Close Scene, Project Settings, Close Project, Quit |
| **Edit** | Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select All, Deselect, Frame Selected, Frame All |
| **View** | the side bar views, Side Bar / Inspector / Panel toggles, panel tabs, Maximize Panel, Editor Layout, Overlays, Snap to Grid, Reset Layout |
| **Scene** | New/Open/Save Scene, Scene Settings, Frame All, Play Scene |
| **Entity** | Create, Create Child, Save as Prefab, Instantiate Prefab, Prefab actions, Duplicate, Delete, Activate/Deactivate, Hide, Lock, Move Up/Down |
| **Component** | Add Component, Remove Component, Browse Components |
| **Build** | Validate Project, Export Game, Run in Player, the Build view, Build Output |
| **Debug** | Play, Pause, Step, Stop, Restart, physics and collider overlays, statistics |
| **Help** | Keyboard Shortcuts, About |

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+Alt+S, Ctrl+W, Ctrl+Q | new scene, open project, save, save as, save all, close scene, quit |
| Ctrl+Tab, Ctrl+Shift+Tab | next, previous scene tab |
| Ctrl+Z, Ctrl+Y (Ctrl+Shift+Z) | undo, redo |
| Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, Delete, Ctrl+A | copy, cut, paste, duplicate, delete, select all |
| W, R, E | Move, Resize, Rotate tool |
| F, Home | frame the selection, frame everything |
| Arrow keys, Shift+arrows | nudge the selection |
| F2 | rename the selected entity |
| Alt+Up, Alt+Down | move among siblings |
| F5, Shift+F5, F6, F10, Ctrl+Shift+F5 | play, stop, pause, step, restart |
| Ctrl+F5 | run in the standalone player |
| Ctrl+B, Ctrl+Alt+B, Ctrl+J | toggle side bar, inspector, panel |
| Ctrl+Shift+E, H, K, X, B | Explorer, Scene, Prefabs, Components, Build view |
| Ctrl+Shift+Y, M, U | Console, Problems, Build Output |
| Ctrl+\ | split the editor area |
| Esc | cancel a drag or a pending pick |

## Recipe: a pressure plate that opens a door

1. **+ > Level > Platform**, place it and resize it into a floor.
2. **+ > Mechanisms > Door** and **Pressure Plate**; move them where they belong.
3. Select the plate. In **PressurePlate > Targets**, press **+ Add** and choose *Door* (or use the
   eyedropper, or drag the door from the hierarchy onto the field). A cyan arrow now runs from the
   plate to the door.
4. Set the plate's **Activator Tags** if only some characters should press it (empty: anything
   that can move), and the door's **Open Offset** by dragging its ghost.

## Recipe: two characters and their world

- **+ > Gameplay > Character** twice. Give them tags and different action sets in their
  **PlayerInput** components (Player1, Player2).
- **+ > Gameplay > Spawn Point** for each, and set **SpawnPoint > Character**; a killed character
  returns there (or to the last **Checkpoint**).
- A **Hazard** kills what matches its **Affects Tags** (empty: any killable entity), so one pool
  can list a tag another character does not have.
- A **Goal** is satisfied while a living entity with its **Required Tag** stands in it. A
  **LevelFlow** lists the goals; the level completes when all are satisfied at once, and
  **Next Scene** chains levels.
- **Collectible**, **Trigger Zone** (raises named events), **Moving Platform** (travel offset,
  optionally only while signalled) round out the set. Select the camera and set **Mode >
  FitTargets** with both characters under **Targets** to keep them in view.

## Testing the editor

`yk_editor --script file.ykscript` drives the real UI with injected mouse and keyboard events and
checks the result; `tests/editor/*.ykscript` are the project's UI tests, run by `ctest -R editor_`.
Widgets register their screen rectangles under stable names (`toolbar/Play`, `hierarchy/Gate`,
`activity/Explorer`, `dialog/Export/export`, `prop/cooldown`, ...), so scripts survive layout
changes. Commands include `click`, `drag`, `key`, `type`, `edit`, `menu File/Save Scene`, `capture`
(a screenshot) and many `expect` checks (selection, properties, links, dialogs, the workbench's
views, files on disk, runtime positions); the language is described at the top of
`editor/ui/EditorDriver.hpp`. Scripts run headlessly (SDL's dummy video driver and software
renderer) on a private copy of the demo project, save screenshots of failed expectations, and
imply `--fixed-step`: every editor frame advances Play by one 1/60 s tick, so `hold d 45` is
0.75 s of game time on a slow or busy machine as on a fast one.
