# ADR 0022: Tilemaps are a component with chunked sparse layers

Date: 2026-10-01. Status: accepted.

## Context

A facility of 200 x 150 tiles on several floors cannot be 30,000 entities, and walls must be
breakable, which a baked-in mesh would make expensive to change.

## Decision

- **`Tilemap`** is a component holding layers of tiles in 32 x 32 chunks stored sparsely (empty
  chunks cost nothing). Tile ids index a **`Tileset`** (`.yktileset`: sheet, size, per-tile flags
  solid/opaque, noise damping, area cost, animation frames, tags and a `modify` block naming the
  tool action, resistance and replacement tile). The map serializes as a JSON property, so scene
  files, prefabs and the editor's snapshot undo carry it with no special case.
- **Collision** is built from solid tiles as merged static bodies per chunk and rebuilt only for
  chunks whose tiles changed (`syncTilemaps` before the physics step). A tile with a custom collider
  rectangle uses it.
- **Rendering** draws the visible chunks of the visible levels with animation; **navigation and
  sight** read the same tile flags through `WorldGrid`.
- **Changes** go through `setTile`, which records an edit log (for save games and the editor) and
  raises dirty rectangles for collision, navigation and the minimap.

## Consequences

Destroying a wall is `setTile`; collision, navigation, sight and saving follow without extra code.
The tile painting tools of the editor are not built yet (NEXT.md); maps are authored in JSON.
