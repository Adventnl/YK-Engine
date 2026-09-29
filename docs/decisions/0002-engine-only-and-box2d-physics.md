# ADR 0002: Engine-only layout and private Box2D solver

Date: 2026-09-27. Status: accepted for the Box2D/physics decisions; directory placement superseded
by ADR 0003; the "engine only, no games" product boundary superseded by ADR 0004.

The user requested the engine itself, removal of all games, a single engine folder,
and a substantial working 2D physics implementation. This explicitly supersedes the
old two-game roadmap and the old prohibition on rigid-body physics.

All project directories, including tests/docs/licenses/build outputs, live in engine/.
Root files remain build entry points. Sandbox source, executable, smoke test and run
target are removed. ApplicationLayer is the generic synchronous runtime callback API.

Use Box2D 3.1.1, statically fetched from a SHA-256-pinned official tagged archive.
The MIT notice is retained. Engine public physics types hide native IDs and keep
Box2D private. Lifetime tokens and 64-bit creation serials prevent stale-handle reuse,
including native 16-bit generation wrap. Box2D supplies collision broad phase,
contact constraints, sleeping and CCD instead of duplicating those systems.

Physics operates in meters/seconds/radians, independently of SDL. Fixed-tick world
stepping reports dropped time. Backend events become copied engine values. Optional
graphics debug drawing explicitly converts meters to renderer units.

A headless configuration builds the same physics/core/input library with no SDL fetch.
Only two joint families are exposed initially; shape casts, controllers, custom contact
policy and additional constraints need future scoped work. The engine is a library,
so consumers supply their own executable and loop.
