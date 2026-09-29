# Editor guide

`yk_editor` is where levels are built. It edits the same scene files the player runs, and Play runs
the scene inside the editor with the same runtime the player uses.

```sh
yk_editor                          # welcome screen
yk_editor path/to/project          # open a project (a folder, or its project.ykproj)
yk_editor path/to/project --scene scenes/level2.ykscene
```

The **Open Sample** button (and `projects/elemental-prototype`) opens the checked-in
*Elemental Prototype*, a good thing to poke at first: play it, then select the lever and see the
arrow to the door it opens.

## Projects, scenes, prefabs

A project is a folder:

```text
project.ykproj      name, window size, start scene, collision layers
scenes/*.ykscene    levels
prefabs/*.ykprefab  reusable entities (an entity and everything under it)
assets/             images and sounds you add (see below)
```

- **File > New Project** asks for a name and a parent folder, and creates the folder, the standard
  collision layers (Default, Solid, Player, Sensor, Prop) and a first scene with a camera.
- **File > Open Project**, **Recent Projects**, **New Scene**, **Open Scene**, **Save Scene**
  (Ctrl+S), **Save Scene As**. Opening something while the scene has unsaved changes asks whether
  to save, discard or cancel; so does closing the window.
- **Entity > Save as Prefab** stores the selected entity and its children in `prefabs/`. Drag a
  prefab from the Assets panel into the Scene view (or double-click it) to place a copy. Prefab
  copies are independent entities: editing the prefab file later does not change placed copies.
- **File > Project Settings** edits the name, window size, start scene and the collision-layer
  matrix (which layers touch, for solid collisions and triggers alike).
- **File > Validate Project** loads every scene and prefab and lists problems: broken files,
  references to missing entities, missing asset files, unknown layers.
- **File > Export Game** makes `<name>-game/` with `yk_player` and a copy of the project's data.
  Run the player from that folder. (The player needs the yk_player program from your build, found
  next to the editor. Projects that use only the built-in and prototype components run as they are;
  a game with its own C++ components needs a player built with them.)

Images and sounds: copy files into the project folder (for example `assets/hero.png`,
`assets/jump.wav`; PNG and BMP images, WAV sounds), press the refresh button in the Assets panel,
and pick them from an asset field's folder button. Until real art exists everything works with
placeholder shapes: a sprite with no texture draws a colored rectangle or ellipse, and sound fields
accept placeholder tones (`tone:660,0.12`) from the same picker.

## The panels

- **Hierarchy**: the scene's entities as a tree. Click to select (Ctrl toggles, Shift selects a
  range), double-click to frame it in the Scene view, drag a row onto another to make it a child
  (onto empty space to move it to the top level), F2 or the context menu to rename, right-click for
  Create Child, Add Component, Duplicate, Delete, Move Up/Down, Save as Prefab. The search box
  filters by name or component. The **+** button creates entities.
- **Scene view**: the level as authored, with collider outlines. Grid, collider outlines, links,
  entity names and the game camera's frame are toggled by the small buttons on top. Coordinates and
  zoom are shown at the top right.
- **Game view**: the level as the game camera sees it (a preview while editing; the running game
  while playing).
- **Inspector**: the selected entity's name, active flag, tags and transform, then one section per
  component with a field per property. Everything in it is generated from the components' own
  declarations, so every component looks and behaves the same way. `...` on a section removes the
  component (refused while another component needs it) or resets it; the checkbox disables it;
  **Add Component** searches all components.
- **Assets**: project files by folder; double-click a scene to open it, a prefab to add it.
- **Console**: everything the engine and editor log, filterable by level and text.

Docking works as usual (drag tab titles); **View > Reset Layout** restores the default.

## Working in the Scene view

| Do this | To |
|---|---|
| Click / Ctrl+click / Shift+click | select / toggle / add to the selection |
| Alt+click | select the next entity underneath |
| Drag empty space | box-select |
| Drag an entity | move it (all selected entities move together) |
| **W / R / E** | Move, Resize, Rotate tool |
| Resize tool: drag a handle | resize; Shift keeps proportions, Alt resizes from the center |
| Rotate tool: drag the knob | rotate (snaps to 15 degrees) |
| Drag the dashed ghost of a door or moving platform | set how far it moves |
| Arrow keys (Shift for more) | nudge |
| **F** / **Home** | frame the selection / the whole scene |
| Mouse wheel | zoom at the cursor |
| Middle or right drag, or Space + left drag | pan |
| Right-click | create here, paste here |
| Hold **Ctrl** while dragging | toggle grid snapping |
| **Esc** | cancel a drag or a pending pick |
| Ctrl+Z, Ctrl+Y | undo, redo (every drag or typed edit is one step) |
| Ctrl+C, X, V, D, Delete | copy, cut, paste, duplicate, delete |

Resizing scales a sprite, its collider and other sized parts together, keeps the entity's pivot at
the same relative place in the box, and leaves children where they are. Duplicating or pasting a
plate together with its door keeps the copy of the plate opening the copy of the door.

## Recipe: a pressure plate that opens a door

1. **+ > Level > Platform**, place it and resize it into a floor.
2. **+ > Mechanisms > Door** and **Pressure Plate**; move them where they belong.
3. Select the plate. In **PressurePlate > Targets**, press **+ Add** and choose *Door*. (Or press
   the eyedropper and click the door in the Scene view, or drag the door from the Hierarchy onto
   the field.) A cyan arrow now runs from the plate to the door.
4. Set the plate's **Activator Tags** if only some characters should press it (empty: anything that
   can move), and the door's **Open Offset** by dragging its ghost.

## Recipe: two characters and their world

- **+ > Gameplay > Character** twice (or the prototype's Fire/Water Character). Give them tags
  (`fire`, `water`) and different keys in **PlatformerController** (left/right/jump).
- **+ > Gameplay > Spawn Point** for each, and set **SpawnPoint > Character**; a killed character
  returns there (or to the last **Checkpoint**).
- A **Hazard** kills what matches its **Affects Tags** (empty: any killable entity), so a lava
  pool can list `water` and a water pool `fire`.
- A **Goal** is satisfied while a living entity with its **Required Tag** stands in it. Put a
  **LevelFlow** (prototype module) on an entity, list the goals, and the level completes when all
  are satisfied at once; **Next Scene** chains levels.
- **Collectible**, **Trigger Zone** (raises named events), **Moving Platform** (travel offset,
  optionally only while signalled) round out the set. Select the **Main Camera** to follow or fit
  both characters (**Mode > FitTargets**, then add them under **Targets**).

## Play, Stop, and what stays yours

**F5** (or the Play button) starts a play session: the editor copies the open scene (including
edits you have not saved) into the runtime and shows it in the Game view, which takes the keyboard.
Both characters are live, mechanisms run, the camera frames as authored. Pause (F6), Step (F10) and
Restart work as the toolbar shows. While playing the Hierarchy and Inspector show the running copy
read-only (click things in the Scene view to inspect their live values), and editing is disabled.
**Shift+F5** stops: the copy is thrown away and the editor is exactly as you left it, including
what was selected and the undo history.

## Automated checks

`yk_editor --script file.ykscript` drives the real UI with injected input and checks results;
`tests/editor/*.ykscript` are the project's UI tests (see `editor/ui/EditorDriver.hpp` for the
language, and `--test-hooks` to record widget positions). Run them with `ctest -R editor_`. They run
headlessly (SDL dummy video driver, software renderer), copy the sample project so it is never
changed, and save screenshots of failures. Scripts imply `--fixed-step`: every editor frame advances
Play by exactly one 1/60 s tick and the UI's clock by 1/60 s, so `hold d 45` means 0.75 s of game
time on a slow or busy machine as on a fast one.

Shortcuts are listed in **Help > Keyboard Shortcuts**.
