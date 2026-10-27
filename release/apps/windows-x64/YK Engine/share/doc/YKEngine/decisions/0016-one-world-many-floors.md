# ADR 0016: One simulation world with many floors; scenes are for separate maps

Date: 2026-10-01. Status: accepted.

## Context

An *Escapists*-class game has a ground floor, upper floors, a roof, vents and tunnels that are all
alive at once: a guard on the ground floor hears a noise on the first floor, an escape route
crosses three of them, a schedule sends 40 characters up and down stairs. Splitting each floor
into its own scene would stop the simulation of everything not on screen, break persistent
identity of characters and force every cross-floor interaction through scene changes.

## Decision

- **One scene is one continuous simulation.** `SceneSettings::levels` (a `WorldLevelSet`) lists the
  floors (`id`, name, kind Floor/Underground/Roof/Vent, elevation). An old scene has none and
  behaves as a single level. Separate scenes remain for separate *maps* (a yard that is not part of
  the building, another facility).
- **An entity is on a level through `WorldLayer`** (an optional component; the nearest one at or above
  the entity decides, `*` means every level). `levelOf(entity)` is the one function everything asks.
- **Physics filters by level.** Shapes of different levels never touch: the Box2D custom filter
  compares the level stored in the shape. `World::setLevel` moves a body; `WorldLayer::moveTo`
  (which raises `level_changed`) is the only run-time way to change a level.
- **Queries carry a level**: `SpatialHash` (and `SpatialIndexService` over it), line of sight,
  navigation, perception and the renderer (`LevelViewMode`: all, focus only, focus and faded below).
- **Floors are connected by data, not by teleport code**: navigation links (stairs, ladders, vents,
  drops) and, in the sim layer, things that move an entity with `moveTo`.

## Consequences

Everything that is spatial takes a level argument and old content passes the default. Tilemaps are
per level (`Tilemap.level`). There is no streaming of levels in or out; a large map is one scene
(measured in STATUS.md) and map streaming remains an extension point (NEXT.md).
