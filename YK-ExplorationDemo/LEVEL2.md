# Level 2: The Inner Keep

The project opens directly into the second chapter. It is a 48 by 28 tile castle prison,
adapted from the supplied `invisible_castle_level2_editable.zip`: north and south cells,
common room, canteen, laundry, control, stores, central corridor, workshop, infirmary,
exercise yard, utility, and the green exit anteroom.

Play with **WASD / arrow keys**, **E** to interact, **Escape** to cancel or close,
**P** to pause, and **R** to restart. The camera follows your character smoothly and
clamps to the castle bounds. Its 12.0833-unit view gives 48 pixels per world unit in the
1280 by 720 window; HUD bands reserve 40 pixels above and 100 below the world.

```sh
./build/dev/yk_player YK-ExplorationDemo
```

To start the first chapter, pass `--scene scenes/courtyard.ykscene`. Its completed
escape now leads into level two; **Enter** skips the completion wait.

## Escape route

1. Leave the north cell through the gold threshold and talk to Mara in the west common room.
2. Search the **tool bench** and **tool chest** in the southwest workshop for a screwdriver
   and wire cutters. Search progress is cancellable and never grants duplicate rewards.
3. Take clean **linen** from the folding table in the north laundry.
4. Use the screwdriver to search the **fuse cabinet** in the northeast stores.
5. At the workshop **repair workbench**, wrap the fuse with linen. Crafting consumes both
   ingredients and creates an insulated fuse with its own newly generated inventory icon.
6. Search the **security desk** in the north control room for the security card. A guard
   patrols between control and the yard. Walls block his sight; leave control before the
   warning fills. Capture returns you to your cell, cancels remote searching, and preserves
   hidden inventory. All six other NPCs provide local hints.
7. Use the **power terminal** in the southeast utility room. **W/S** chooses the left wire,
   **A/D** the right terminal, and **E** connects them. Match both the letter and color.
   Incorrect pairs can be retried; **Escape** closes the puzzle.
8. With power restored, use the wire cutters at the **green gate**, then walk south through
   the opening. Restart clears all collected supplies, repairs, and gate state.

## Editable data and artwork

`scenes/level2.ykscene` contains 433 individually editable entities. All movement,
searches, inventory, crafting, dialogue, patrols, puzzle and exit behavior are configured
with reusable engine components. There is no game-specific runtime code.

All shipping raster artwork for this chapter was generated anew with the built-in
image-generation tool. The supplied atlas, extracted props and reconstructed floor
layers are references only. No source-reference pixels are shipped. Eight original masters,
their exact prompts, and prompt metadata are preserved in `assets/level2/source/`:

- `furniture.png`: sixteen complete furnishing designs.
- `equipment.png`: sixteen control, workshop, medical and courtyard designs.
- `details.png`: sixteen decorations and item designs.
- `tiles.png`: sixteen regenerated floor/wall materials.
- `hero.png` and `guard.png`: four directions, with walk and reaching cycles.
- `residents.png`: six NPCs, each with four directional poses.
- `safe_fuse.png`: a complete cloth-wrapped fuse icon.

The exporter finds transparent gutters around complete newly generated objects rather
than truncating them at idealized grid coordinates. Character frames share one scale and
registered feet. Textures retain generated alpha and use nearest-neighbor sampling.
Exact room plaques are code-authored bitmap text. Floor tiles preserve one slab per world
unit; walls, furniture and actors have authored solid footprints and connected openings.

Rebuild the assets and scene without calling an image service:

```sh
python3 YK-ExplorationDemo/tools/build_level2.py
./build/dev/yk validate YK-ExplorationDemo
./build/dev/yk_castle_level2_tests
```

Generate the first chapter with `tools/complete_level.py` before running the level-two
builder when rebuilding both chapters. The latter selects level two as the start scene.

Gameplay checks cover camera following and bounds at three viewport sizes, physical
travel through the rooms, cancelled searches, unique rewards, crafting consumption,
patrol capture during a search, puzzle prerequisites and incorrect matches, the solid
exit chain, complete escape, and restart. Native renderer captures include the starting
cell, workshop, east wing, compact window, and an overview. The overview uses a temporary
fixed camera for inspection; the shipping scene always follows the player.
