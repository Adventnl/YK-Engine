# Invisible Castle: Inner Keep extension

This document records the completed second chapter as an ordinary extension of the
established native SDL game. [PRODUCT.md](../PRODUCT.md) owns product truth;
[DESIGN.md](../DESIGN.md) remains the normative source of inherited design tokens.
The existing [design.json](design.json) is preserved. This document scopes the new
chapter's adaptations without replacing the project identity or changing global tokens.

## Overview

The Inner Keep retains the Invisible Castle visual grammar: blue masonry and coping,
amber timber, brass details, blue-and-gold heraldry, dark square interface bands,
bitmap lettering, complete character silhouettes and visible physical escape work.
The supplied castle reference guides the room layout and material variations. Shipping
art is newly generated; reference pixels are not exported into runtime sprites.

Thirteen connected rooms occupy a 48 × 28 world-unit map. The first view is an immediately
playable north cell with an empty six-slot inventory. Room-specific furnishings, exact-text
plaques and six residents' local hints establish wayfinding. The escape sequence connects
workshop tools, laundry linen, a stored fuse, ingredient-consuming insulation, the control
room security card, utility repair and the chained green exit. The chapter is editable
scene/data content built with reusable engine components.

## Colors

Inherit DESIGN.md's `stone`, `masonry`, `wall-cap`, `wood`, `parchment`, `brass`, `hud`,
`text`, `action`, `muted-text`, `inventory-slot`, `progress-track` and `guard-warning`
roles and values. Their frontmatter remains authoritative; do not recopy or independently
retune those primitives here.

The new floors are scoped material variations: pale blue cells, deep blue common room,
warm canteen stone, grey laundry/stores, dark control and utility, light corridor/exit,
amber workshop, mint infirmary and a grass-and-sand exercise yard. The chapter's amber
workshop floor adapts the incumbent workshop treatment to the supplied reference. These
are authored bitmap materials, not replacements for the global `workshop` color token.
Blue masonry, gold thresholds and consistent furnishings bind the rooms together.

The green gate and generator indicate the repair/exit mechanism. Wire terminal colors
belong to the puzzle's four labeled pairs; they do not introduce a new application palette.
Always show A–D labels alongside color, selection markers and connected-state feedback.

## Typography

Continue using the native YK BitmapFont and inherited title/body/label scale roles.
The HUD and search/result panels retain the existing bitmap hierarchy. Room plaque text
is authored deterministically with Pillow's default raster font, a dark blue ground,
brass outline and parchment lettering. Generated lettering is not used for exact room names.

The wire puzzle and completion screen use larger bitmap headings, short instructions
and explicit keyboard actions. Keep message text readable at compact native window sizes.

## Layout

World geometry remains on the integer grid with one world unit per floor slab. Floor
textures span four units with four slabs; do not mistake texture dimensions for a new
world grid. Wall and furnishing silhouettes must agree with their solid footprints,
with open circulation and doorways at least two cells wide.

The second chapter uses a player-following camera, unlike the first chapter's documented
overview-scale presentation. Its observed configuration in `tools/build_level2.py` is:

| Setting | Level-two value |
| --- | --- |
| Mode / target | `Follow` / Explorer |
| Orthographic height | `12.083333` world units |
| Smooth time | `0.08` seconds |
| Viewport inset | `[0, 140]` reference pixels |
| Clamp bounds | `[0, 0]` to `[48, 28]` |
| World scale at 1280 × 720 | 48 screen pixels per world unit |

Persistent HUD bands retain 40 reference pixels above the world and 100 below it.
The shared UI unit remains `min(viewport width / 1280, viewport height / 720)`.
The camera derives its aspect from the world area remaining after those scaled insets,
so its clamp must account for the reserved HUD at resized windows. Its height remains a
world-space measure; the 48-pixel zoom is specific to the reference window.

`review/overview.png` uses a temporary fixed inspection camera. It documents room
connectivity and material placement, not the shipping camera. Preserve Follow in the scene.

## Elevation & Depth

Retain nearest-neighbor sampling, dark pixel outlines, highlights, coping, furniture
legs, authored ground shadows and base-aligned y sorting. The world gets its depth from
bitmap art rather than soft interface shadows. Search/result overlays retain the dark,
nearly opaque ground and action-gold top rule. The wire puzzle dims the world beneath
a square dark-blue panel; selected terminal rows lighten within that material family.

## Shapes

Square world tiles and square UI corners remain inherited. Complete new object exports
are located by transparent gutters rather than idealized atlas-cell cuts. Keep alpha
around each silhouette and preserve visible object bases.

Hero and guard animation cells are **32 × 44 pixels**, a scoped adaptation of the
incumbent **32 × 40** cells. Each 8-column, 4-row sheet has down/left/right/up rows and
four walk plus four reaching frames per row. One scale is shared across each character's
frames; feet register at cell pixel 42. The scene presents actors at `1 × 1.375` world
units with a base-aligned offset. Resident sheets also use 32 × 44 directional cells.
Retain the small feet collider instead of making the full hair/body silhouette solid.

## Components

**Persistent HUD and inventory.** Inherit the header/footer, six item wells, readable
objectives, proximity prompts and control hints. Use the same generated item artwork in
the world, inventory and loot result. Empty slots represent actual empty inventory.

**Timed work.** Searches and crafting face the object and show repeated reaching plus
cancellable progress. Walk clips retain 9 fps and reaching clips 6 fps. Ordinary searches
take 2.2 seconds, fuse insulation takes 2.5 seconds and cutting the gate chain takes
3 seconds. Cancellation grants no reward; repeated searches do not duplicate items.
Fuse insulation consumes the linen and spare fuse and returns the distinct safe-fuse icon.

**Recoverable patrol.** The guard moves between control and the yard. Restricted control
room exposure uses the inherited warning color, status copy and suspicion bar. Walls
block sight. Capture returns the player to the cell, cancels remote work and preserves
hidden inventory; this persistence is part of PRODUCT.md's behavior contract.

**Wire puzzle.** The utility terminal requires the insulated fuse and security card.
Show four letter-and-color source/terminal pairs, a visible selection marker and completed
connection feedback. W/S selects a source, A/D a destination, E connects and Escape closes.
Incorrect pairs remain retryable. The square panel and bitmap type inherit the established
interface; this larger modal is specific to the repair state.

**Green exit and completion.** Repair power before chain cutting becomes available.
The closed gate seals the two-unit perimeter opening, including its edges. Successful
work retracts the visible gate and disables its solid collider together; walking south
then triggers the chapter-specific completion copy. R clears inventory, repairs and gate
state. The first chapter can continue into this chapter; this chapter opens directly by default.

## Do's and Don'ts

- **Do** inherit the root token roles and native bitmap interface when adding chapter content.
- **Do** use colored room materials for local wayfinding within the shared castle grammar.
- **Do** preserve alpha gutters, complete silhouettes, registered feet and matching collisions.
- **Do** check follow-camera bounds against the rendered world viewport at resized windows.
- **Do** communicate wire matches with letters and color together.
- **Don't** apply the incumbent 32-pixel overview zoom or 32 × 40 animation-cell assumption to level two.
- **Don't** smooth pixel textures or export pixels from the supplied reference assets.
- **Don't** use a diagnostic screenshot as proof of a completed physical escape route.

## Provenance and evidence checked

Eight masters and their matching exact prompt files exist in `assets/level2/source/`:
`furniture`, `equipment`, `details`, `tiles`, `hero`, `guard`, `residents` and `safe_fuse`.
`prompts.json` records all eight generation prompts. The exporter writes provenance and
prompt metadata into native textures. Rebuild artwork and scene with `tools/build_level2.py`;
the source masters and authoring tools are excluded from runtime builds.

Documentation was compared with PRODUCT.md, DESIGN.md, design.json, `level2-brief.md`,
`level2-quality-bar.md`, LEVEL2.md, the level-two builder, native renderer treatments and
`tests/unit/castle_level2_tests.cpp`. The tests separately cover camera following/bounds
at three sizes, physical room passages, search cancellation and unique rewards, crafting
consumption, captured-search cancellation, inventory persistence, puzzle prerequisites and
mistakes, the sealed gate, escape and restart. The implementation task reports those
gameplay checks passed; this documentation pass did not rerun them.

All ten native captures were opened and checked:

| Capture under `review/` | Evidence |
| --- | --- |
| `start.png` | Playable north cell, new hero, furnished rooms and empty inventory |
| `workshop.png` | Workshop/infirmary material distinctions and visible furnishing bases |
| `east.png` | Yard, utility approach, plaques and floor transitions |
| `compact.png` | Compact native window with readable HUD and close world view |
| `overview.png` | All thirteen rooms and connected openings; inspection camera only |
| `search.png` | Hero reaching toward bench, progress and cancellation hint |
| `result.png` | Readable reward message and matching inventory/result icon |
| `puzzle.png` | Active labeled/color terminal modal; diagnostic state capture |
| `warning.png` | Patrol warning, suspicion bar and visible control-room context |
| `completion.png` | Open gate and configured completion copy; diagnostic state capture |

The captures show their named states without blank/black capture failures and retain the
incumbent material language. Active puzzle/completion captures demonstrate presentation;
physical traversal and the escape sequence have separate gameplay-test evidence.
The final reviewer disposition was **ship**, at the scope of the resolved PRODUCT.md
inventory-persistence documentation fix. That verdict is recorded at its stated scope,
not expanded into an additional whole-surface certification. No HTML/CSS detector ran:
this is native SDL, so that detector has no applicable verdict.

## Preserved metadata drift

The inherited sidecar's inventory and result previews still draw the screwdriver using
SVG primitives, whereas DESIGN.md describes regenerated bitmap artwork. Its
`nativePresentation` source, review screenshots and 32 × 40 character metadata refer
to the first chapter. Those entries do not describe the completed second chapter and
must not override the scoped adaptations above. The original DESIGN.md and sidecar
were preserved; repairing their existing preview drift was outside this extension's scope.
