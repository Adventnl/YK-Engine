# Blackthorn Keep

The project now starts in **Level 2: The Inner Keep**, a furnished thirteen-area castle prison
with freshly generated art, a character-following camera, crafting, guard patrols, a utility
wire puzzle and a green exit gate. See [LEVEL2.md](LEVEL2.md) for its route and editable assets.

Residents now walk local routines and breathe while idle. Room names appear on larger plaques
and in the header. E opens and closes room doors with visible sliding motion; unlocked gates
can be closed and reopened. Conversations show detailed close-up character portraits, and
Escape dismisses them while keeping the game running.

The original chapter described below remains playable with
`--scene scenes/courtyard.ykscene`; completing it leads into level two.

A playable castle-prison escape game built in YK Engine. The warden has imprisoned you for
refusing his taxes. Mara knows a drainage passage beneath the east tower. Find tools, make a
grappling line, and slip past the patrol before you climb out.

```sh
./build/dev/yk_player YK-ExplorationDemo
```

Or open `project.ykproj` in the editor and press Play. Move with WASD or arrows. Press **E**
near a person, container, workbench or gate. **Escape** cancels a search or closes its result;
**E** dismisses loot results and advances conversations. **R** restarts the level.

1. Leave your cell through the south opening and talk to Mara in the common room.
2. Search the tool bench in the south workshop for a **screwdriver**.
3. Pry open the locked cabinet in the north stores for **rope**.
4. Search the south laundry chest for an **iron hook**.
5. At the rope workbench, craft a **grappling line**. This consumes the rope and hook.
6. Search the warden desk in the guard office for the **brass key**. Wait for the guard to
   leave: he patrols during searches, and the warning meter fills when he sees you trespassing.
   Leave the office to lose his attention. If caught, you return to your cell with your hidden
   supplies intact.
7. Unlock the iron gate at the east end of the common room.
8. Use the screwdriver and grappling line at the drain grate. When it retracts, walk south
   into the drainage passage to escape.

The objective is a hint, not a prerequisite: supplies may be collected in any order when you
have the necessary tool. Every search and crafting action has a progress bar, a directional
reach animation, cancellation, a result popup and a visible item icon. The six inventory slots
show actual engine `Inventory` contents. A cancelled action grants nothing; searched
containers cannot duplicate items. Objects cannot be searched through walls.

The art follows the supplied castle references: cool blue flagstones, beveled slate walls,
warm timber, gold thresholds, burgundy rugs, royal banners and rounded RPG characters.
The built-in image generation tool created independent source atlases; no artwork was cropped
from the user references. Complete generated masters and exact prompts are preserved in
`assets/keep/source/`. The export step slices only those new masters, preserves alpha and uses
nearest-neighbor sizing with registered character feet and fixed torch mounting plates.

`tools/complete_level.py` rebuilds the scene and exports the textures deterministically through
`tools/import_reference_style.py`, using Pillow. It does not call a generation service:

```sh
python3 YK-ExplorationDemo/tools/complete_level.py
./build/dev/yk validate YK-ExplorationDemo
./build/dev/yk_exploration_tests
```

An actual player render is saved as [reference-style-preview.png](reference-style-preview.png).

Torches stay attached to walls while their flames animate. Furnishings and prisoners have
static collision bodies; the guard has a kinematic patrol, and the player has a solid feet
collider. The HUD occupies reserved screen bands outside the world view. The original hall
is retained as an optional legacy scene; it is not part of this escape route.

The gameplay regression test checks actual directional frames, fixed props, gate seams,
wall occlusion, room doorways, locked and cancelled searches, inventory rewards, crafting
consumption, patrol capture, the complete escape and clean restart. The editor smoke test
checks loading, inspection and Play/Stop.
