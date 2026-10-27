# Level 2: The Inner Keep

A furnished castle prison assembled from `invisible_castle_level2_editable.zip` in YK Engine.
The supplied five-layer composition guides the connected rooms, wall elevations and colors.
The authored plan removes about a quarter of the overall footprint while preserving native
furniture and character sizes. The canteen, laundry and infirmary lose at least a quarter
of their floor area. Characters and every world material use 35 pixels per world unit.
Furniture is exported from three unified pixel-art atlases with one camera view and palette.

Storage and work surfaces form wall runs; the common room has a table and seats instead of
spare beds. Infirmary nightstands share the beds' head line and sit within four pixels of
their sides. Extra barrels, chairs and duplicated furnishings are removed. Clear aisles
connect all eighteen passages, and resident routines follow the compact plan. The final
gate frame reaches the base of the outer wall without a black strip below it.

```sh
./build/dev/yk_player games/invisible-castle
```

Move with **WASD / arrows**, interact with **E**, dismiss or cancel with **Escape**,
pause with **P**, and restart with **R**. Room and corridor passages have no doors; walk straight through.
The camera follows the player with a closer 9.2-unit view and clamps to the keep boundaries.
The world is rasterized at 35 pixels per unit with nearest sampling at every window size.
HUD text and dialogue retain display resolution.

## Escape route

1. Leave the north cell and talk to Mara in the west common room.
2. Search the southwest workshop's tool bench and tool chest for a screwdriver and wire cutters.
3. Take clean linen from the folding table in the north laundry.
4. Pry open the northeast stores' fuse cabinet with the screwdriver.
5. Wrap the fuse with linen at the workshop repair workbench.
6. Search the security desk for a security card while the guard is away. Leave control before
   the warning fills; walls and furniture block sight. Capture cancels the search and returns
   you to the cell without taking hidden tools.
7. Use the southeast utility terminal. **W/S** selects the left wire, **A/D** the right terminal,
   and **E** connects them. Match both letter and color. Incorrect pairs can be retried.
8. With power restored, cut the green exit's chain using the wire cutters, open the gate,
   and walk south. Restart clears supplies, repairs and gate state.

Every search is cancellable and grants its contents once. Crafting consumes ingredients.
NPC conversations use detailed portraits and give local hints.

## Shared visual and collision rules

Both chapters use `assets/level2/` directional character sheets and the supplied castle
materials in `assets/castle/pixel/` and native masonry in `assets/castle/materials/`.
Room name plaques and the HUD's room label are gone.
Residents and guards use kinematic bodies with solid feet colliders: they follow their
routines and cannot be shoved out of place by walking into them. A speaker pauses while
others continue their routines.

`SpriteRenderer` contact shadows stay anchored to the feet while sprites walk, reach or
breathe. The source wall/furniture artwork retains its authored shadowing. Warm candle and
hearth glows use `Light2D`. Banners and wall-mounted controls render over their wall faces.

Ordinary room and corridor openings have no door artwork, interaction or blocking door
colliders. Locked escape gates retain their requirements and animated leaves.
The current gate and banner masters and their prompts remain under
`assets/castle/source/repairs/consistency/`.

Walls share a joined footprint with chapter one: 35-pixel coping width in both directions,
a shorter 28-pixel brick face, and reusable 35 x 35 blocks. Collision covers the complete
visible wall silhouette. Passage cuts clear artwork and physics together, including the
small connecting cell passages. Walls and fixtures draw below characters so the player
cannot disappear behind them. The planar union removes internal end caps and overlapping
surfaces at corners and T junctions. Banners stay on uncut masonry faces. Carpets retain
both halves of their native 45-pixel pattern. Room floor materials reach the front edge
of each doorway, where the next room begins. Side passages align with the wall face.
The wire terminal and exit switch sit in the two service bays above the final anteroom;
the lower divider gives both controls room while keeping the central exit aisle clear.
The yard planter is
clear of the utility doorway. The hero's separate `hero-polished` atlas adds four standing and
blinking directions without changing the existing walk and search cells. Movement stops on
release, and resolved physics velocity selects walking versus idle when pushing against walls.

## Editable assets and rebuild

The supplied manifest records source origins and pivots. Scenery stays individually editable;
it is not a flattened background. `tools/castle_reference.py` imports its placements;
`tools/castle_masonry.py` builds joined wall blocks; `tools/castle_geometry.py` shares
furniture sizes and physical footprints
with chapter one. `tools/castle_pixel_art.py` exports native sprites and bakes exact opaque-pixel
collision into compound boxes; `tools/castle_layout.py` owns deliberate placements and clear aisles.
Oversized equipment is reduced, repeated furniture uses canonical assets, and banners always
use one fixed size. Redundant chairs at wall openings are removed. Scene
logic still uses reusable engine components for inventories, crafting, patrols, dialogue,
wire matching and level flow.

```sh
python3 games/invisible-castle/tools/complete_level.py
python3 games/invisible-castle/tools/build_level2.py
./build/dev/yk validate games/invisible-castle
ctest --preset dev -R invisible_castle_chapter2
```

Rebuilds use the retained current artwork. Source masters and prompts are excluded from game exports.

Gameplay checks verify all eighteen ordinary passages have no doors and physically traverse
them in both directions. They also cover NPC push resistance and motion, modal dismissal,
three camera viewport sizes, searches, crafting, guard capture, the wire puzzle, the sealed
exit, completion and restart. Additional collision checks walk into beds, benches and the machine
from exposed sides and verify every collider piece exists in the physics world.
The Python art checks compare shipping collision to texture alpha pixel by pixel, enforce
native sizes and filters, reject furniture overlaps, and sample every NPC route against
scenery collision in both chapters.
The renderer test checks actual contact-shadow pixels, optional raster cameras,
one-pixel UI details, offset viewports and resizing within an outer render target.
