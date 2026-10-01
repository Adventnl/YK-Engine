# ADR 0017: Services and ordered update phases

Date: 2026-10-01. Status: accepted.

## Context

Large simulations need things that belong to the running game as a whole (clock, navigation, spatial
index, rules, random dice, loot, definitions) and need their relative order to be predictable. The
platformer engine ran every component's `onFixedUpdate` in hierarchy order, so inter-system
behaviour depended on how a scene happened to be authored. Global singletons would have made tests
interfere and made restarts unclean.

## Decision

- **`Services`** (one per `GameRuntime`) hold typed services created on first use
  (`context.services().get<T>()`), started immediately, ticked once per fixed tick in the service's
  phase *before* the components of that phase, and shut down in reverse creation order when the
  runtime rebuilds or ends. A service may save state (`saveKey/saveState/loadState`) and describe itself
  to the Debug panel. Ownership is the runtime's; there are no globals.
- **Components declare an `UpdatePhase`** (`TypeBuilder::updatePhase`). Order within a tick:
  `Clock`, `PreUpdate`, `Decision`, `Gameplay` (the default: every old component), `Steering`,
  `Motor` | physics step, transform write-back, triggers, collisions | `Perception`,
  `PostSimulation`, then the destroy flush, event dispatch and the interpolation snapshot.
  Within a phase components run in hierarchy order.
- **Events are queued and delivered at safe points** (end of the tick and after the variable update),
  with wildcard (`crime.*`, `*`), a payload (`Json`), delayed delivery and a dispatch bound.

## Consequences

A system that needs another's results is placed in a later phase instead of relying on authoring
order. Tests construct a runtime with `MemoryAssets` and get their own services. A service created
mid-tick starts ticking on the next tick. The phases are listed in ARCHITECTURE.md.
