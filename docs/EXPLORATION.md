# Exploration engine pass

## Audit

The engine already had a reusable scene/component registry, prefab serialization, fixed-step
Box2D physics, named input sets, animation clips and controllers, bounded cameras, a world renderer
with explicit layers and culling, a blackboard, event bus, signals, and scene transitions. The
platformer game uses those systems and remains a separate data-only project.

The missing pieces for a top-down game were a zero-gravity four-direction controller, a way for a
player to select a nearby object, NPC movement, multi-page conversation data and portrait UI,
openable barriers whose collision follows their state, named arrival points on map transitions,
and depth ordering within a world layer. The existing `Door`, `Lever`, `TriggerZone`, `EventAction`,
`SpawnPoint`, `LevelFlow`, camera, physics, and input map remain useful for platformers. The new
components supplement them rather than replacing their behavior.

| Need | Reusable implementation |
|---|---|
| Free exploration | `TopDownController` uses a zero-gravity body and MoveUp/Down/Left/Right actions; diagonals are normalized. |
| NPCs | `NpcPath` follows editor-placed waypoint entities, loops or stops, waits at points, and can be gated by a blackboard flag. Empty path means stationary. |
| Interactions | `Interactor` picks the closest `Interactable` in range, publishes `{interaction_prompt}`, and handles one-time actions, conditions, event emission, flag changes, dialogue, gates and portals. |
| Dialogue | `Dialogue` loads `.ykdialogue` pages or inline text, drives typewriter/advance input, locks movement during playback, and emits start/page/finish events. `SceneRenderer` presents a portrait and text over the live world. Each page may select a different high-resolution portrait. |
| Barriers | `StateGate` opens by interaction or signal, may require a flag to unlock, changes collider filtering as it animates, and can retain its state across maps. Existing moving/sliding `Door` remains available. |
| Maps | `MapPortal` changes scenes and carries a named arrival; `MapSpawn` positions the player in the new area. The blackboard's kept flags survive scene changes. |
| Depth | `SpriteRenderer.ySort` uses the entity's world Y and optional offset within an explicit layer. Background, world, foreground, effects and UI layers remain designer-controlled. |
| Editor | All components and fields use registry reflection, so the existing inspector, scene files, prefab workflow, validation, undo and Add Component menu work with them. Exploration entity templates appear in the placement catalog. `.ykdialogue` files appear in the asset picker. |

This pass intentionally uses 2D collision and SDL rendering. The demo maps are small and fully
loaded; the engine already culls off-screen sprites. NPCs have paths rather than complex AI.

## Try the second test game

```sh
./build/dev/yk validate YK-ExplorationDemo
./build/dev/yk_player YK-ExplorationDemo
./build/dev/yk_editor YK-ExplorationDemo
```

Installed editors also offer **Open the Exploration Demo** on the welcome screen.

Move with WASD or arrows; press E or Space to interact and advance dialogue. Talk to Mara, activate
the brass switch, open the gate, and walk through the hall entrance. The hall has an inscription and
a return doorway. The two portraits in Mara's dialogue are separate from her small world sprite.
The demo's input map assigns Interact to E/Space so S and Down remain movement keys; when converting
an existing platformer project, adjust its action bindings in Project Settings the same way.

## Authoring a conversation

Attach `Dialogue` and `Interactable` to any entity. Set `Dialogue.sequence` to a `.ykdialogue` file,
or fill `speaker`, `pages` and `portrait` inline. A sequence is JSON:

```json
{
  "pages": [
    {"speaker": "Keeper", "text": "Hello.", "portrait": "assets/portraits/keeper.png"},
    {"speaker": "Keeper", "text": "Come inside.", "portrait": "assets/portraits/keeper_smile.png"}
  ]
}
```

Set the entity's `Interactable.prompt`, `range`, optional `requiredFlag` and optional `setFlag`.
A switch can use `setFlag` and `once` without any custom class. A `StateGate` can use that flag as
its `unlockFlag`, plus a `persistFlag` if its open state should survive map changes. A touch portal
needs a trigger `Collider` and `MapPortal.onTouch`; an interaction portal needs `Interactable`.
Place a `MapSpawn` with the same name as `MapPortal.spawn` in the destination scene.
When a one-time interaction has a `setFlag`, it remains used when the player returns to that map.

For directional art, provide `idle_up/down/left/right` and `walk_up/down/left/right` clips in an
`AnimatedSprite` animation asset. The controller selects those names when no animation controller
asset is assigned; with a controller asset it publishes `moveX`, `moveY` and `moving` instead.
