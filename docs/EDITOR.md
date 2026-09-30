# Editor guide

`yk_editor` is where levels are built. It edits the same scene files the player runs, and Play runs
the scene inside the editor with the same runtime the player uses.

On a Mac the editor is **YK Engine.app** (from the `.dmg`, see [BUILDING.md](BUILDING.md#macos)):
double-click it, or double-click a `.ykproj` file, and nothing else is needed. Elsewhere it is the
`yk_editor` program. Command line:

```sh
yk_editor                           # welcome screen: New Project, Open Project, recent projects, the demo game
yk_editor path/to/project           # open a project (a folder, or its project.ykproj)
yk_editor path/to/project --scene scenes/level2.ykscene
yk_editor --size 1920x1080          # window size
yk_editor --ui-scale 1.25           # make the interface larger (accessibility, unusual displays)
yk_editor --debug-crash segv        # crash on purpose to see the crash report (abort, segv, throw)
```

A project path that cannot be opened is reported in a dialog and the welcome screen stays.

**Where things are kept.** The editor's log, crash reports and settings live in the per-user folders
of the system, never next to the program: on macOS `~/Library/Logs/YKEngine/Editor/` (`editor.log`,
earlier runs as `editor.1.log` ...; `crash-editor-*.txt`) and `~/Library/Application Support/YKEngine/Editor/`
(recent projects, `workbench.json`); on Linux `$XDG_STATE_HOME/YKEngine/Editor/logs` and
`$XDG_DATA_HOME/YKEngine/Editor`; on Windows `%LOCALAPPDATA%\YKEngine\Editor\Logs` and
`%APPDATA%\YKEngine\Editor`. **Help > Open Logs Folder** opens it. `YK_LOG_DIR` replaces the log
folder. If the last session did not end normally, the next start says so once, names the crash
report and offers to open the folder.

The welcome screen offers **Open the Demo Game** (Cinder Vale, in `YK-DemoGame/`): open it, press
**F5**, then select the lever and see the arrow to the gate it opens. The demo is a normal project;
nothing in the editor knows about it.

## The workbench

The window follows the layout and the look of a current VS Code: thin outlined, softly rounded
cards on a darker canvas, a cool neutral palette, small type, no decoration.

```text
+--------------------------------------------------------------------------------------------+
|  File Edit View Scene Entity Component Build Debug Help  [ > || >| [] <-  | Cinder Vale v ]   |  title bar
| +----+---------------------+-------------------------------------------+------------------+ |
| |    | EXPLORER        ... | level01.ykscene *  |  Game        [split][...]| Inspector  Debug | |
| | A  | > Open Scenes       +-------------------------------------------+                  | |
| | c  | v Cinder Vale       |  move resize rotate | snap 0.5 m | ...    | (an entity, a    | |
| | t  |   assets            |  Cinder Vale > Terrain > Ground            |  file, the scene,| |
| | i  |   prefabs           |                                            |  or the running  | |
| | v  |   scenes            |            scene view / game view          |  game's state)   | |
| | i  |                     +-------------------------------------------+                  | |
| | t  |                     | Console  Problems  Build Output  Profiler |                  | |
| | y  |                     |  [info] Opened scene scenes/level01.ykscene|                  | |
| +----+---------------------+-------------------------------------------+------------------+ |
|  Cinder Vale   level01.ykscene *   0 problems             171 entities  Snap 0.50 m  60 fps  |  status bar
+--------------------------------------------------------------------------------------------+
```

- **Title bar**: the menus, and a capsule holding the **Play / Pause / Step / Stop / Restart**
  controls and the **project name**. The project name is a real switcher: Open, New, Recent
  Projects, Project Settings, Close. On the right, buttons show or hide the side bar, the panel and
  the right card.
- **Activity bar** (far left, in the same card as the side bar): one icon per side bar view. Click
  the visible one again to hide the side bar.
- **Side bar**: **Explorer** (project files, and an *Open Scenes* list to switch between or close
  scenes), **Scene** (the entity tree), **Prefabs**, **Components** (a catalog of every component
  with its description) and **Build** (validate, run, export). The `...` in each view's header opens
  that view's own actions (Explorer: import, refresh, reveal in the file manager; Scene: select,
  frame).
- **Editor area**: tabs. Every open scene has a tab (a dot marks unsaved changes) and the **Game**
  view has one. The split button, or *View > Editor Layout > Split Right / Split Down*, puts the
  Game view beside or below the scene so you can edit and watch at once; close its tab to unsplit.
- **Right card**: two tabs. **Inspector** shows the selected entity, or the file picked in the
  Explorer, or (when nothing is selected) the scene's own settings. **Debug** shows what is
  running: the tick, game time and body counts of the play session, the Blackboard variables the
  gameplay library publishes (level rules, HUD values), and the selected entity's live position,
  velocity and overlaps. It has a Play button while nothing runs; nothing on it is invented, and
  what the runtime cannot report is not listed.
- **Panel**: **Console** (everything the engine and editor log, filterable by level and text),
  **Problems** (the last project check), **Build Output** (export and run messages) and
  **Profiler** (frame times, draw statistics, physics numbers while playing).
- **Status bar**: project and scene, the problem count (click to open Problems), entity and
  selection counts, snapping, zoom and frame rate. It turns orange while Play runs and yellow while
  paused, and shows *Player running* while a standalone player started with **Run in Player** is
  open (the editor tracks that process: **Build > Stop Player** ends it, and quitting the editor
  ends it too).

Every divider is draggable, and every region can be collapsed with the title bar buttons. Sizes, the visible views, the split and which panel tab is open are
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
| Drag the orange pin of a hinge (or any point field marked as a pin) | move its anchor; it is stored in the entity's own space, so it follows a turned entity |
| **F** / **Home** | focus the selection / fit everything you can edit |
| Mouse wheel | zoom at the cursor (a sideways wheel or two-finger swipe pans) |
| **Ctrl+=**, **Ctrl+-**, **Ctrl+0** | zoom in, zoom out (round levels: 25, 50, 75, 100, 150, 200 ... %), back to 100% |
| Middle or right drag, or Space + left drag | pan (the cursor shows a hand while Space is held, four arrows while dragging) |
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

The toolbar above the view has the tools, snapping and its grid size, an **Overlays** menu (grid,
collider outlines, sprite bounds, pivots, links of the selection or all links, entity names, the
game camera's frame) and the view controls: **zoom out**, the **zoom level** (click it for
25% ... 800%, *Fit the Scene* and *Focus the Selection*), **zoom in**, **fit** (Home) and **focus**
(F). The same commands are in the **View** menu, and the zoom level is also in the status bar
(click it to fit the scene). Zooming from a button or a key is about the middle of the view; the
wheel zooms about the pointer, so the spot you point at stays put. The view controls work while the
game plays, too (put the scene view beside the Game tab with **Split Right**, since Play shows the
Game tab). An editor group too narrow for the whole row (a split) keeps the tools, the snap
switch and the zoom and folds the grid size, overlays, fit and focus into a **...** menu. The line
below the toolbar is a breadcrumb of the selection's place in the hierarchy; click a part to
select it.

SDL 3.2 reports no trackpad pinch gesture, so pinch-to-zoom is not available; a two-finger scroll
zooms like the wheel and a sideways swipe pans.

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
(Open, Set as Start Scene, Add to Scene, Copy Path, Show in File Manager, Rename or Move, Delete).
The search box opens the folders that contain matches.

**Rename or Move** (**F2** on the selected row) asks for the new path inside the project; typing a
path with folders moves the file there, and moving a folder moves everything in it. Scenes,
prefabs, animations and the project's start scene refer to files by path, so the dialog says how
many references will be rewritten and in which files, and the rewrite is all or nothing: a file that
cannot be written undoes the rest. A picture's import settings (`.ykmeta`) move with it, and the
open scenes are reloaded from the rewritten files (unsaved scenes are dealt with first; the undo
history of the reloaded scenes starts again). **Delete** (**Del**) asks first, listing what would be
left dangling; those references then show up in the Problems panel. Neither operation can be
undone, and neither is offered while the game is playing.

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
  so an update replaces their own changes (other than name and placement). It reaches every
  scene of the project: the open ones change in the editor (unsaved, undoable), the ones that are
  not open are written to disk at once, and the dialog says which is which.
- **Unpack Prefab** forgets the link; **Show Prefab in Explorer** finds the file.

## Play, Stop, and what stays yours

**F5** (or the Play button) starts a play session: the editor copies the open scene (including
edits you have not saved) into the runtime and shows it in the Game view, which takes the keyboard.
Every action set is live (Player1 and Player2 at once), mechanisms run, the camera frames as
authored. Pause (F6), Step (F10) and Restart work as the controls show. While playing, the
hierarchy and Inspector show the running copy read-only, and editing is disabled. **Shift+F5**
stops: the copy is thrown away and the editor is exactly as you left it, including what was
selected and the undo history. **Debug > Run in Player** (Ctrl+F5) starts the standalone player on
the project instead, as a separate process the editor keeps track of: the status bar says *Player
running*, **Build > Stop Player** ends it, a second Run replaces it, and quitting the editor
closes it so no helper is left behind.

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
in `templates/<system>/` beside the editor (the macOS application carries a macOS player, so a Mac
exports for macOS with nothing else). The Build view lists which systems are ready. The steps go to
**Build Output**, and the finished dialog can show the folder.

For macOS the result is `GameName.app`: the game's name and version, its icon (**Project Settings >
Build > App icon**, a square PNG of at least 512 px, converted to `AppIcon.icns`) and copyright,
and no editor. On a Mac the dialog also offers **Sign** (ad hoc, or with the identity you type,
which is what a release needs) and **Disk image (.dmg)**; off a Mac these are disabled and say
why. The same options are `yk export --dmg --sign`. Notarization needs your Apple credentials
and is not done by the editor or `yk export`; [BUILDING.md](BUILDING.md#macos) lists the commands
to run on an exported game.

## Menus

| Menu | What is in it |
|---|---|
| **File** | New/Open/Recent Project, New/Open/Save/Save As/Save All/Close Scene, Project Settings, Close Project, Quit |
| **Edit** | Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select All, Deselect, Frame Selected, Frame All |
| **View** | the side bar views, Side Bar / Inspector / Panel toggles, panel tabs, Maximize Panel, Editor Layout, Overlays, Snap to Grid, Zoom In / Zoom Out / Reset Zoom, Reset Layout |
| **Scene** | New/Open/Save Scene, Scene Settings, Frame All, Play Scene |
| **Entity** | Create, Create Child, Save as Prefab, Instantiate Prefab, Prefab actions, Duplicate, Delete, Activate/Deactivate, Hide, Lock, Move Up/Down |
| **Component** | Add Component, Remove Component, Browse Components |
| **Build** | Validate Project, Export Game, Run in Player, Stop Player, the Build view, Build Output |
| **Debug** | Play, Pause, Step, Stop, Restart, physics and collider overlays, statistics |
| **Help** | Keyboard Shortcuts, Open Logs Folder, About |

## Keyboard shortcuts

On a Mac read **Ctrl** as **Cmd** and **Alt** as **Option** (the menus and the shortcut list show
the Mac spelling); the exception is scene-tab switching, which is **Control+Tab** because Cmd+Tab
belongs to the system.

| Keys | Action |
|---|---|
| Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+Alt+S, Ctrl+W, Ctrl+Q | new scene, open project, save, save as, save all, close scene, quit |
| Ctrl+Tab, Ctrl+Shift+Tab | next, previous scene tab |
| Ctrl+Z, Ctrl+Y (Ctrl+Shift+Z) | undo, redo |
| Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, Delete, Ctrl+A | copy, cut, paste, duplicate, delete, select all |
| W, R, E | Move, Resize, Rotate tool |
| F, Home | focus the selection, fit everything |
| Ctrl+=, Ctrl+-, Ctrl+0 | zoom in, zoom out, back to 100% |
| Arrow keys, Shift+arrows | nudge the selection |
| F2 | rename the selected entity (in the Hierarchy) or rename or move the selected file (in the Explorer) |
| Alt+Up, Alt+Down | move among siblings |
| F5, Shift+F5, F6, F10, Ctrl+Shift+F5 | play, stop, pause, step, restart |
| Ctrl+F5 | run in the standalone player |
| Ctrl+B, Ctrl+Alt+B, Ctrl+J | toggle side bar, inspector, panel |
| Ctrl+Shift+E, H, K, X, B | Explorer, Scene, Prefabs, Components, Build view |
| Ctrl+Shift+Y, M, U | Console, Problems, Build Output |
| Ctrl+\ | split the editor area |
| Esc | cancel a drag or a pending pick, close an open menu or popup |

## Recipe: a pressure plate that opens a door

1. **+ > Level > Platform**, place it and resize it into a floor.
2. **+ > Mechanisms > Door** and **Pressure Plate**; move them where they belong. Put the plate's
   origin on the floor: the slab stands 0.14 above it and sinks into the floor under a load. The
   plate is a real surface: a character can drop onto it, stand on it and ride it down, and a
   crate pushed onto it presses it too.
3. Select the plate. In **PressurePlate > Targets**, press **+ Add** and choose *Door* (or use the
   eyedropper, or drag the door from the hierarchy onto the field). A cyan arrow now runs from the
   plate to the door.
4. Set the plate's **Activator Tags** if only some characters should press it (empty: anything
   that can move), **Minimum Mass** to ignore light things, and the door's **Open Offset** by
   dragging its ghost (or **Open Rotation** for a hinged door).
5. Want an activation zone instead of a button (a region in the floor that has nothing to stand
   on)? Give the plate a trigger collider: it senses whoever overlaps the zone, whatever blocks
   movement (set **Sensing** to *Region* to say so explicitly). The **Problems** panel tells you
   when a plate has no pad to stand on (or no trigger for *Region*), or no targets.

## Recipe: a seesaw, a swinging bridge

Add a **Collider** and a **RigidBody** (Dynamic), then **HingeJoint**. An orange **pin** appears in
the scene view at the anchor; drag it to where the plank should pivot (it snaps to the grid like
everything else). **Connected Body** empty pins the plank to the world, or name another body.
**Limits** keep the swing between two angles (degrees from where you placed it, positive
clockwise); **Spring** pulls back toward **Rest Angle** so a seesaw returns to level; **Motor**
turns it. Characters standing on it are carried by the point they stand on.

## Recipe: a level with a beginning and an end

Add **Level Flow** (**+ > Gameplay**). List the **Goals**; set **Intro Duration** and **Intro
Message** for a "get ready", **Time Limit**, **Restart On Death**, the **Complete Message**,
**Complete Delay**, **Next Scene** and the variables to carry into it (**Keep Variables**: a gem
count). While the level ends, controls are locked and whoever stands in an exit walks into it.
**Continue Action** (an action of the global set, such as Enter) skips the wait. For things that
should happen *because* of something (open a hatch two seconds after the third gem, restart a
platform every ten seconds), use **+ > Gameplay > Event Action**: pick the event, the delay and what
to do.

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
views, files on disk, runtime positions, the view's zoom and center); the language is described at
the top of `editor/ui/EditorDriver.hpp`. Scripts run headlessly (SDL's dummy video driver and software
renderer) on a private copy of the demo project, save screenshots of failed expectations, and
imply `--fixed-step`: every editor frame advances Play by one 1/60 s tick, so `hold d 45` is
0.75 s of game time on a slow or busy machine as on a fast one.
