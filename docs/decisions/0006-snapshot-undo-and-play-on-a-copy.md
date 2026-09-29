# ADR 0006: Snapshot undo with change transactions; Play runs a copy

Date: 2026-09-29. Status: accepted.

## Context

The editor needs reliable undo/redo for every kind of edit (property changes, gizmo drags, structural
edits, component changes) and a Play/Stop cycle that returns to exactly the pre-Play state.
Command objects per edit type would have to be written and kept correct for each feature.

## Decision

`EditorDocument` records history as serialized scene snapshots. An edit happens inside a *change*:
`beginChange` stores the current serialized scene and selection; `endChange` compares the new
serialization and, only when different, pushes one undo step (labelled, up to 200). A drag or a
focused text box holds one change open, so it is a single step; a change that ends unchanged leaves
no trace. Undo and redo rebuild the scene from the snapshot. Dirty state compares document versions,
so undoing back to the saved state is clean. Because rebuilding replaces every object, code that
outlives a frame holds `EntityId`s.

Play mode serializes the document (including unsaved edits) into a fresh scene owned by a
`GameRuntime`. Stopping destroys it. The edit document, its selection and its undo history are never
touched by play.

## Consequences

Every current and future edit is undoable for free, provided it goes through a change. Cost is
proportional to scene size and is paid once per committed change, not per frame (a drag or a typed
edit is one change). Measured in a Release build on the development VM, with every entity holding a
sprite and a collider, a committed edit takes about 1.5 ms at 100 entities, 9 ms at 500, 41 ms at
2,000 and 0.24 s at 10,000 (undo and redo cost about the same); the sample level has 47 entities.
Large scenes would first reuse the previous snapshot instead of serializing both before and after
(about half the cost), then move to per-entity deltas; both fit behind the same
`beginChange`/`endChange` interface. Play sees exactly what the save format can represent, so
anything that would not survive a save also would not survive Play.
