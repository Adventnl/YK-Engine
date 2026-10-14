---
name: Blackthorn Keep
description: An Escapists-style pixel castle with visible, solid escape interactions.
colors:
  stone: "#82aec1"
  masonry: "#344b70"
  wall-cap: "#a3b9d5"
  workshop: "#82aec1"
  wood: "#946035"
  parchment: "#f5deab"
  brass: "#e7b34d"
  hud: "#16191f"
  text: "#e1dabe"
  action: "#e5b561"
  muted-text: "#a7a795"
  inventory-slot: "#2e3135"
  progress-track: "#3d3c35"
  guard-warning: "#f57d65"
typography:
  title:
    fontFamily: "YK BitmapFont"
    fontSize: "14px"
    lineHeight: "18px"
  body:
    fontFamily: "YK BitmapFont"
    fontSize: "12.6px"
    lineHeight: "16.2px"
  label:
    fontFamily: "YK BitmapFont"
    fontSize: "8.4px"
    lineHeight: "10.8px"
rounded:
  square: "0px"
spacing:
  tile: "32px"
  hud-inset: "20px"
  search-inset: "16px"
  result-inset: "24px"
components:
  hud-header:
    backgroundColor: "{colors.hud}"
    textColor: "{colors.action}"
    typography: "{typography.title}"
    rounded: "{rounded.square}"
    height: "40px"
  hud-footer:
    backgroundColor: "{colors.hud}"
    textColor: "{colors.text}"
    rounded: "{rounded.square}"
    height: "100px"
  inventory-slot:
    backgroundColor: "{colors.inventory-slot}"
    rounded: "{rounded.square}"
    width: "52px"
    height: "48px"
  search-progress:
    textColor: "{colors.action}"
    rounded: "{rounded.square}"
    width: "330px"
    height: "100px"
  search-result:
    textColor: "{colors.text}"
    rounded: "{rounded.square}"
    width: "610px"
    height: "170px"
---

# Design System: Blackthorn Keep

## Overview

**Creative North Star: "Blackthorn Keep"**

The user brief fixes the direction: an Escapists-style pixel castle, with coherent rooms,
solid geometry, real four-direction animation, visible tools and a connected escape story.

The game is a native SDL desktop experience. Its compact, authored pixel rooms make movement,
physical obstacles and escape work readable in the same view.

**Key Characteristics:**
- Coherent castle rooms on one tile grid.
- Solid furnishings with visible, aligned footprints.
- Four-direction movement and timed reaching/searching.
- Persistent HUD outside the playfield; recognizable tools and explicit result messages.

## Colors

Cool stone and masonry establish the keep; timber, parchment and brass identify its furnishings
and useful objects. Warm action copy separates active instructions from ordinary HUD text.

### Primary
- **Action gold** (`action`): objectives, search titles, progress fill and the game title.
- **Brass** (`brass`): locks, rope artwork, keys and furniture details.

### Secondary
- **Timber** (`wood`): the shared furniture material.
- **Parchment** (`parchment`): authored signs and small prop details.
- **Guard warning** (`guard-warning`): the watching status and suspicion bar.

### Neutral
- **Stone**, **masonry** and **wall coping** (`stone`, `masonry`, `wall-cap`): the floor and castle shell.
- **Workshop floor** (`workshop`): workshop and laundry floor tiles.
- **HUD ink** (`hud`): opaque screen bands; temporary search panels use the same RGB with alpha 250/255.
- **Warm text** and **muted text** (`text`, `muted-text`): readable messages and quieter control hints.
- **Inventory slot** and **progress track** (`inventory-slot`, `progress-track`): fixed item wells and unfilled progress.

## Typography

The interface uses the engine's built-in **YK BitmapFont**, a 5 × 7 pixel ASCII glyph atlas,
with a six-pixel character advance and nine-pixel line advance at scale 1. The frontmatter
records glyph heights and line advances at the reference viewport: header title at scale 2,
result body and interaction prompt at scale 1.8, and footer hints at scale 1.2. Native text
scales by the same viewport unit as its panel; it does not use a browser font stack.

Search title scales are 1.5 while busy and 2.3 for results. Search messages use 1.2 and 1.8
respectively, with authored action copy wrapped at spaces to 48 columns. Room signs use
Pillow's default raster font baked into their plaque textures.

## Layout

The game is a native SDL desktop experience. The world uses 32-pixel tiles at one world unit;
all floor origins and wall footprints sit on integer coordinates. At the default window size,
the camera shows 18.125 world units in the reserved 580-pixel-high play area, giving an exact
32-pixel tile scale. Retina rendering doubles those pixels without smoothing.

The HUD reserves 40 pixels above the world and 100 below at 1280 × 720. Interface placement and
text scale follow the viewport; objectives and supplies never cover the moving game.
The shared scale unit is `min(viewport width / 1280, viewport height / 720)`.

There are cells, locked stores, a guard office, a common room, a workshop, laundry and an east
tower with drainage. Furniture belongs to those rooms. Doors are two tiles wide and perimeter
walls are continuous. Sprite bases align with collision footprints; room signs sit on masonry.

## Elevation & Depth

Pixel highlights, coping, dark outlines and visible furniture legs convey depth. Character
art has a small authored translucent ground shadow; sprites use base-aligned y sorting.
HUD bands are opaque, while search/result overlays have alpha 250/255 and a two-pixel action
gold top rule. There are no soft UI shadows or animated panel lifts. Generated candles and flames include
small transparent warm glows. Gold thresholds mark doorways; banners, plants and armor statues
add reference-style detail without obstructing the escape route.

## Shapes

World tiles are square and UI panels have square corners. Furniture uses distinct, dark-outlined
silhouettes with solid bases. Character cells are 32 × 40 pixels and tool icons are 32 × 32.

## Components

### Persistent HUD

The header contains the game title and guard status. The footer contains the objective or
notice, nearby interaction prompt and control hints. Six inventory slots use 52 × 48-pixel
wells spaced 92 pixels apart, with 36 × 36-pixel item icons and names beneath. These measures
are at the reference viewport and scale with the shared unit.

### World animation

Torch mounts are static. Only their flame animation changes.

Character sheets have 8 columns and 4 rows: down, left, right and up. Each row supplies four
walk cells and four reaching/searching cells, with a direction-specific idle. Walk clips run
at 9 fps, searching at 6 fps, and the four-cell torch flame at 7 fps. Torch cells are 32 × 48 pixels; every frame
uses the same mounting pixels, with the upper flame region supplying the animation.

Level-two residents use 12-column sheets: an idle pose and two alternating strides for each
of four directions. Walking plays stride, neutral, opposite stride, neutral at 8 fps. Their
room routines have different waits; breathing and gentle sway continue while standing. During
conversation only the speaker stops; background residents keep moving.

Ten ordinary room doors begin open and slide beside the wall. E closes or reopens them,
with collision held until the opening motion finishes and closing deferred while the threshold
is occupied. Unlocking the green exit and opening it are separate states; cutting the chain
happens once, and subsequent E presses toggle the visible gate.

All thirteen level-two areas publish their room name to the HUD header. Plaques use their
authored aspect ratio at a .75-world-unit height and render above doors and furniture.

### Conversation popup

The dialogue panel is 1040 by at least 300 reference pixels, centered above the footer.
A 232-pixel-wide portrait well holds a newly generated detailed bust. The speaker uses scale
2.5, word-wrapped body copy scale 1.8, and dismissal hints scale 1.4; all follow the shared
viewport unit. A restrained 180 ms arrival and darkened backdrop focus attention on the
character. E continues or confirms a choice; Escape closes the conversation without quitting.

### Search and result panels

Search progress
appears in the upper-right area; the player remains visible at the workshop. Loot results
show a distinct icon, readable message and explicit dismissal action. Inventory icons have
matching artwork in the world and the result panel.

The busy panel is 330 × 100 pixels, placed 350 pixels from the viewport's right edge and 60
from its top. Its progress track is 298 × 6 pixels. The result panel is 610 × 170 pixels,
centered horizontally at `viewport height - 286 × unit`, with a 48 × 48-pixel item icon.
Temporary panels overlay the world. The locked cabinet uses the same result treatment to
explain that a screwdriver is required. Results show `E - CONTINUE / ESC - CLOSE`; busy
searching shows `ESC - CANCEL`.

The signature interaction is visible, timed work: the prisoner turns toward a cabinet or
bench and reaches repeatedly while progress advances. Escape cancels without granting items.
The guard continues patrolling, making the office search a decision about timing. A completed
drain action retracts its visible grate and solid collider together before the victory trigger
can be reached.

Ordinary container searches last 2.2 seconds, tying the grappling line lasts 2.5 seconds,
and opening the drain lasts 3 seconds. The story connects the screwdriver, rope, iron hook,
crafted grappling line and brass key to the escape route.

## Do's and Don'ts

### Do:
- **Do** align floor origins and wall footprints to the integer world grid.
- **Do** align visible sprite bases with their solid collision footprints.
- **Do** keep objectives, supplies and persistent control hints in the reserved HUD bands.
- **Do** use the same recognizable tool artwork in inventory and result panels.
- **Do** show directional reaching and progress during timed searching.

### Don't:
- **Don't** smooth the authored pixel textures.
- **Don't** move torch mounting plates with their animated flames.
- **Don't** grant search items when the player cancels.
- **Don't** leave the drain's visible grate or solid collider in place after it opens.

World art was regenerated with the built-in image generation tool to follow the supplied
castle references: blue flagstones and masonry, richly shaded amber timber, royal blue and
gold heraldry, burgundy rugs, terracotta ferns and rounded chibi characters. Reference images
were used for style only; no reference pixels were extracted. The original generated atlases
and exact prompts live in `assets/keep/source/`. `tools/import_reference_style.py` exports
complete generated objects into native engine textures, with transparent alpha and nearest
sampling. `tools/complete_level.py` rebuilds this art and the scene without network access.
Only exact-text room plaques are drawn programmatically. The old source-art directory remains
for the optional legacy hall.
