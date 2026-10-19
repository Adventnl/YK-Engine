# Spear-rack style consistency pass

These fourteen individual transparent masters were generated with the built-in
ImageGen tool using `../../../props/corridor_weapon_rack_east.png` as the style
reference. Each adjacent `.prompt.txt` preserves its exact prompt. The prompts
specify complete isolated silhouettes, detailed amber wood and steel, dark
outlines, upper-left light and the same elevated RPG camera as the spear rack.

| Master | Used for |
| --- | --- |
| [banner.png](banner.png) · [prompt](banner.prompt.txt) | Every wall banner, including visible iron mounting brackets |
| [crate.png](crate.png) · [prompt](crate.prompt.txt) | All single crates in both chapters |
| [crate-pair.png](crate-pair.png) · [prompt](crate-pair.prompt.txt) | Stores' pair, at the same crate height as the singles |
| [bench.png](bench.png) · [prompt](bench.prompt.txt) | Both courtyard benches |
| [candle-nightstand.png](candle-nightstand.png) · [prompt](candle-nightstand.prompt.txt) | Both infirmary bedside candle cabinets |
| [stool.png](stool.png) · [prompt](stool.prompt.txt) | Yard, utility, workshop and infirmary stools |
| [green-console.png](green-console.png) · [prompt](green-console.prompt.txt) | Solid power puzzle terminal |
| [exit-switch.png](exit-switch.png) · [prompt](exit-switch.prompt.txt) | Solid emergency exit switch cabinet |
| [control-tower.png](control-tower.png) · [prompt](control-tower.prompt.txt) | Security control tower |
| [control-console.png](control-console.png) · [prompt](control-console.prompt.txt) | Second security console |
| [laundry-switch.png](laundry-switch.png) · [prompt](laundry-switch.prompt.txt) | Wall-mounted laundry switch |
| [medical-terminal.png](medical-terminal.png) · [prompt](medical-terminal.prompt.txt) | Infirmary instrument cabinet |
| [fence.png](fence.png) · [prompt](fence.prompt.txt) | Complete exercise enclosure, with a clear right-side entrance |
| [gate.png](gate.png) · [prompt](gate.prompt.txt) | Detailed locked gates, exported as eight-frame opening atlases |

`tools/castle_prop_repairs.py` packages these and the earlier furniture masters at
140 pixels per world unit, retaining alpha and the entire silhouette. Runtime
dimensions and export dimensions share `PROP_SIZES`; resizing a canvas cannot
stretch its object. Shared aliases make repeated objects use exactly the same art
and size. `castle_reference.py` exports banners at the same density and fits each
one inside its supporting wall's face, below the coping.

Generated masters stay here; runtime PNGs and linear-sampling metadata live under
`assets/castle/props/` and `assets/castle/fixtures/`. The original reference package
remains preserved. Rebuild both chapters using the commands in the game README.

The fence received an additional ImageGen entrance edit; its exact follow-up
prompt is in [fence-entrance-edit.prompt.txt](fence-entrance-edit.prompt.txt). Its footprint leaves a traversable
side aisle, and the collision exporter measures the visible right-side opening
from alpha rather than assuming the old extraction's gap. The laundry switch is
mounted on the wall face; the power terminal and exit cabinet stand on the floor
with real collision. Exit banners frame the gate on the full-height outer wall.
