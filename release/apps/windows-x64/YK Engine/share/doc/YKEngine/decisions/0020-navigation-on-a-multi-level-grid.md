# ADR 0020: Navigation is a multi-level grid with links, not a navmesh

Date: 2026-10-01. Status: accepted.

## Context

The maps this engine targets are tile based, several floors high, and change while the game runs
(a wall is dug through, a door is locked, a crate is pushed into a corridor). A navmesh would have
to be rebuilt around every change and says nothing about areas that cost more, doors that need a
key, or stairs.

## Decision

- **`WorldGrid`** is a grid per level with solid/opaque flags, area costs, blocker counts (several
  things may block a cell) and noise damping. It also answers line of sight and sound transmission
  by grid traversal.
- **`NavigationWorld`** runs A* over it: eight-way movement without cutting corners, area costs,
  agent clearance (radius in cells), **links** between cells (stairs, ladders, vents, drops, with
  their own costs, one-way or both, and level changes), **doors** with access bitmasks and open
  state, partial paths to the nearest reachable cell, a path cache keyed on grid revision, and
  **resumable searches with a node budget** so a tick never pays for a whole search.
- **Changes mark dirty rectangles**; a world whose grid changed invalidates cached paths and agents
  repath lazily.
- **`NavigationService` and `NavigationAgent`** (gameplay library) own the request queue, share the
  per-tick budget fairly, steer along the path with local avoidance of other agents, open doors
  (asking for a key when locked) and hand velocity to `CharacterMotor`.

## Consequences

Tile maps feed the grid directly (tile properties); entities add blockers by component. Exact
quality limits are in STATUS.md (`navigation` and `navigation_agent` suites).
