# Blackthorn Keep

Two connected castle escape chapters built in YK Engine. Both share the supplied castle's
blue stone, timber furnishings, characters, and a close character-following camera.
The project opens in **Level 2: The Inner Keep**. Chapter one remains available with
`--scene scenes/courtyard.ykscene`; completing it leads into chapter two.

```sh
./build/dev/yk_player games/blackthorn-keep
./build/dev/yk_player games/blackthorn-keep --scene scenes/courtyard.ykscene
```

Move with **WASD / arrows**. Press **E** near people, containers, workbenches or locked gates.
**Escape** cancels a search or dismisses a conversation. **P** pauses; **R** restarts.
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
Chapter two uses their native placements at 35 pixels per world unit. Floors, walls and props
remain separate editable entities. Doorways are cut into reconstructed wall sections to make
all rooms reachable. Chapter one uses the same floor, wall and furnishing materials.

Ordinary passages are clear openings in the stone walls, with no door artwork or blocking
components. Locked escape gates use animated `StateGate` leaves and synchronized collision.
Vertical walls use short sections from shared atlases for local depth sorting;
banners share their supporting wall's depth. Clean column tiles and banner silhouettes remove
the floor scraps baked into the earlier extracted art.

ImageGen masters and their exact prompts are preserved in `assets/castle/source/` and
`assets/level2/source/hero_idle.*`. Runtime outputs are `assets/castle/doors/`,
`assets/castle/fixtures/` and `assets/level2/hero-polished.*`.

Generated with the built-in image_gen tool:

- [Standing hero master](assets/level2/source/hero_idle.png) and [prompt](assets/level2/source/hero_idle.prompt.txt).
- [Door master](assets/castle/source/door-simple.png) and [prompt](assets/castle/source/door-simple.prompt.txt).
- [Clean banner master](assets/castle/source/banner-clean.png) and [prompt](assets/castle/source/banner-clean.prompt.txt).

Rebuild both scenes and the door strip without regenerating existing character atlases:

```sh
python3 games/blackthorn-keep/tools/complete_level.py
python3 games/blackthorn-keep/tools/build_level2.py
./build/dev/yk validate games/blackthorn-keep
ctest --preset dev -R blackthorn_chapter1
ctest --preset dev -R blackthorn_chapter2
python3 games/blackthorn-keep/tools/capture_previews.py
```

Pillow is required for authoring. `--regenerate-art` explicitly re-exports the earlier generated
atlases; normal scene rebuilds preserve them. 

![Chapter one player view](level1-preview.png)
![Chapter two player view](level2-preview.png)

![Open corridor passages and banners](level2-corridor-preview.png)
