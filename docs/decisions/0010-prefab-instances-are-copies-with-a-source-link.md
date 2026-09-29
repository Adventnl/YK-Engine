# ADR 0010: Prefab instances are full copies that remember their source

Date: 2026-09-29. Status: accepted.

## Context

Level building is prefab-driven: a torch, a brick block or a lever is placed many times. Engines
handle prefab edits either by referencing the prefab from the scene and storing per-instance
overrides, or by copying.

## Decision

Placing a prefab copies its entities into the scene with fresh ids and records the prefab's path
on the copy's root (`"prefab"` in the scene file). Scenes therefore contain everything the game
needs, the runtime ignores the link, and a prefab file can change or disappear without breaking a
scene. The editor uses the link for **Revert to Prefab**, **Apply to Prefab**, **Update Other
Instances** and **Unpack**. Reverting keeps the root's identity (id, name, transform, place in the
hierarchy) so references to it stay valid, and keeps references the instance held to entities
outside itself. There is no override data, so reverting or updating replaces an instance's own
changes; bulk updates ask first and reach every scene of the project (the open ones as undoable
edits, the others as files written at once).

## Consequences

The data model stays small and merge-friendly, and every operation is one undo step. What is lost
is Unity-style overrides: changing one instance's color and then changing the prefab's size cannot
keep both. Override tracking (a per-instance list of changed properties) is the natural next step
if needed; the `prefab` link is already the anchor for it.
