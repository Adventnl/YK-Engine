# Blackthorn Keep

Two connected castle escape chapters built in YK Engine. Both share the supplied castle's
blue stone, timber furnishings, characters, and a close character-following camera.
The project opens at its title screen. Click **Play** (or press **Enter**) to choose either
**Level 1: The Outer Hall**, the smaller introductory keep, or **Level 2: The Inner Keep**,
the full castle. Both chapters are available immediately; completing the first leads to the second.
Click **How to Play** for controls and escape tips. Keyboard chapter shortcuts are **1** and **2**;
controller shortcuts are South and North. Enter starts chapter one from the selector.

```sh
./build/dev/yk_player games/blackthorn-keep
./build/dev/yk_player games/blackthorn-keep --scene scenes/courtyard.ykscene
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

The 11.6-unit camera view shows more of the surrounding rooms and passages.
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

`assets/castle/reference/` contains the supplied editable ZIP's PNGs and original manifest.
The original source art uses 35 pixels per world unit. Both chapters now render directly at
the display resolution, preserving source detail instead of discarding it into a smaller
intermediate image. Thirty-six repaired furniture sprites use twice the source resolution;
stone materials use four times the source resolution and preserve complete coping and bases.
The blue corridor runner also has a clean cloth texture without embedded floor patches.
Floors, walls and props remain separate editable entities.

Ordinary passages are clear openings in the stone walls, with no door artwork or blocking
components. Locked escape gates use animated `StateGate` leaves and synchronized collision.
`tools/castle_geometry.py` owns reusable wall lengths, finished returns, furniture dimensions
and floor footprints for both chapters. Wall runs use tiled nine-slice materials: changing a
length repeats the stone rather than stretching an image or clipping a wall end. Openings
split those runs into complete pieces with matching collision. Vertical walls are joined runs with finished free ends;
banners share their supporting wall's depth. Clean column tiles and banner silhouettes remove
the floor scraps baked into the earlier extracted art. Repeated beds, chairs, crates, barrels,
plants and racks use shared silhouettes and dimensions. The utility machine fits inside its room
and its collision covers the pipe assembly. Furniture and walls have grounded contact shadows.

ImageGen masters and their exact prompts are preserved in `assets/castle/source/` and
`assets/level2/source/hero_idle.*`. Runtime outputs are `assets/castle/doors/`,
`assets/castle/fixtures/`, `assets/castle/materials/`, `assets/castle/props/` and `assets/level2/hero-polished.*`.

Generated with the built-in image_gen tool:

- [Standing hero master](assets/level2/source/hero_idle.png) and [prompt](assets/level2/source/hero_idle.prompt.txt).
- [Door master](assets/castle/source/door-simple.png) and [prompt](assets/castle/source/door-simple.prompt.txt).
- [Clean banner master](assets/castle/source/banner-clean.png) and [prompt](assets/castle/source/banner-clean.prompt.txt).
- [Furniture masters, exact prompts and export notes](assets/castle/source/repairs/README.md).
- [Detailed masonry masters and exact prompts](assets/castle/source/masonry-hq/README.md),
  exported by `tools/castle_masonry_art.py`.

Rebuild both scenes and the door strip without regenerating existing character atlases:

```sh
python3 games/blackthorn-keep/tools/complete_level.py
python3 games/blackthorn-keep/tools/build_level2.py
python3 games/blackthorn-keep/tools/build_frontend.py
./build/dev/yk validate games/blackthorn-keep
ctest --preset dev -R blackthorn
python3 games/blackthorn-keep/tools/capture_previews.py
python3 games/blackthorn-keep/tools/capture_previews.py --audit
```

The frontend builder writes ordinary editable scenes and packages its chapter previews under
`assets/ui/`. Chapter builders preserve pause controls, completion flow and the title as the start scene.
The optional visual audit captures every room in both chapters and both overviews at
2560 × 1440 under `build/blackthorn-visual-audit/`, with a framing manifest.

Pillow is required for chapter authoring. `--regenerate-art` explicitly re-exports the earlier generated
atlases; normal scene rebuilds preserve them. 

![Chapter one player view](level1-preview.png)
![Chapter two player view](level2-preview.png)

![Open corridor passages and banners](level2-corridor-preview.png)
