# Invisible Castle

Two connected castle escape chapters built in YK Engine. Both share the supplied castle's
blue stone, timber furnishings, characters, and a close character-following camera.
The project opens at its title screen. Click **Play** (or press **Enter**) to choose either
**Level 1: The Outer Hall**, the smaller introductory keep, or **Level 2: The Inner Keep**,
the full castle. Both chapters are available immediately; completing the first leads to the second.
Click **How to Play** for controls and escape tips. Keyboard chapter shortcuts are **1** and **2**;
controller shortcuts are South and North. Enter starts chapter one from the selector.

```sh
./build/dev/yk_player games/invisible-castle
./build/dev/yk_player games/invisible-castle --scene scenes/courtyard.ykscene
```

Move with **WASD / arrows**. Press **E** near people, containers, workbenches or locked gates.
**Escape** cancels a search or dismisses a conversation; otherwise **Escape / P** pauses.
The pause menu freezes the world, patrols and search progress. Click **Resume**, **Restart Level**,
**Level Select**, or **Main Menu**, or choose with **Up / Down + Enter** (D-pad + South on a controller).
**R** restarts, **M** returns to the title, and **Tab** opens chapter selection.
Completion screens wait for **Enter**: chapter one continues to the Inner Keep, and chapter two
returns to the title. Restart and menu shortcuts also work on completion screens.
Ordinary room and corridor passages have no doors; walk straight through.
Locked escape gates retain their key, tool or puzzle requirement.

The 9.2-unit camera view keeps the player and nearby passages close, about 26% larger than before.
Room plaques and the current-room header have been removed. Characters use the same
level-two directional sheets in both chapters, with grounded contact shadows. The hero has
separate standing and blinking frames in every direction. Releasing movement stops immediately;
holding movement against a wall keeps the character idle. Furniture is
solid; residents and guards have kinematic bodies so the player cannot push them off their routines.

## Chapter one route

1. Talk to Mara in the common room.
2. Search the south workshop's tool bench for a screwdriver.
3. Pry open the north stores' locked cabinet for rope.
4. Search the laundry chest for an iron hook.
5. Make a grappling line at the rope workbench.
6. Search the warden desk for the brass key while the guard is away.
7. Unlock the east gate, prepare the drain grate, and walk into the drainage passage.

Searches and crafting use the engine's actual inventory, progress, cancellation and rewards.
A guard catches exposed trespassers and returns them to the cell while preserving hidden supplies.
See [LEVEL2.md](LEVEL2.md) for the second chapter's route and artwork details.

## Artwork and authoring

Both chapters use the current 35-pixels-per-world-unit sprites, shared furniture silhouettes,
joined masonry and directional characters. Runtime artwork is under `assets/castle/pixel/`,
`assets/castle/masonry/`, `assets/castle/doors/`, `assets/keep/` and `assets/level2/`.
The chapter images in `assets/ui/` are used by the title and level selector.

Only source images and repaired sprites required by the current builders are retained.
Their prompts remain alongside the source artwork. Historical art variants, unused imports,
review captures and duplicate previews have been removed.

Rebuild scenes using the retained artwork (requires Pillow):

```sh
python3 games/invisible-castle/tools/complete_level.py
python3 games/invisible-castle/tools/build_level2.py
python3 games/invisible-castle/tools/build_frontend.py
./build/dev/yk validate games/invisible-castle
ctest --preset dev -R invisible_castle
python3 games/invisible-castle/tests/pixel_art_tests.py
```

`tools/castle_layout.py` owns furniture placement; `tools/castle_masonry.py` builds shared
wall blocks and collision. The builders preserve menus, pause controls and completion flow.
The old atlas reimport mode has been removed; edit the current textures directly.

Optional screenshots are written under the ignored build directory:

```sh
python3 games/invisible-castle/tools/capture_previews.py
python3 games/invisible-castle/tools/capture_previews.py --audit
```
