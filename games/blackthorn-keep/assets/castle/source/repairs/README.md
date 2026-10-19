# Complete castle prop masters

The four transparent masters were repaired with the built-in ImageGen tool. The
exact prompts and input contact sheets are preserved alongside each master. The
original reference extraction remains unchanged under `../../reference/`.

`layout.json` lists each master's objects in reading order. Run
`python3 games/blackthorn-keep/tools/castle_prop_repairs.py` from the repository root
to package the complete silhouettes under `assets/castle/props/` at four times the source
pixel dimensions. Transparent gutters determine extraction boundaries, so uneven
generator spacing cannot chop an object at an assumed cell edge. Runtime metadata
uses 140 pixels per unit and linear sampling to retain the finer detail.

The [spear-rack consistency pass](consistency/README.md) adds fourteen individual
ImageGen masters for remaining extracted props and banners. Shared size definitions
keep the export canvas and world dimensions in agreement, and repeated racks,
benches, stools and crates use shared silhouettes across both chapters.

The replacements remove floor ledges, neighboring-furniture slivers, disconnected
strips and abruptly severed feet from the kitchen, laundry, security room,
infirmary, workshop and common-room props. They also refine the frequently reused
beds, chairs, barrels, plants and canteen dining furniture. All four laundry basins
share one complete basin; candle stools share one complete stand. The chapter-one
drain grate receives a clean silhouette as well.

`corridor-runner-master.png` repairs tan floor patches embedded in the extracted
runner. Its separate export is a continuous opaque royal-blue cloth plane with
complete gold edging and rectangular ends, sized 1320 x 90 pixels with an
8-pixel nine-slice border. The runner's original placement and world size remain
authored by the scene builders.

Both scene builders call the exporter through their shared castle style pass.

| Master | Exact built-in ImageGen prompt |
|---|---|
| [Kitchen and common room](prop-repair-master-1.png) | [Prompt](prop-repair-master-1.prompt.txt) |
| [Laundry, security and infirmary](prop-repair-master-2.png) | [Prompt](prop-repair-master-2.prompt.txt) |
| [Workshop and yard](prop-repair-master-3.png) | [Prompt](prop-repair-master-3.prompt.txt) |
| [Beds, seating and dining furniture](prop-repair-master-4.png) | [Prompt](prop-repair-master-4.prompt.txt) |
| [Continuous corridor runner](corridor-runner-master.png) | [Prompt](corridor-runner-master.prompt.txt) |
