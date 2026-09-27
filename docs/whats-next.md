# What's Next

## Next objective

Finish M1's outstanding hands-on verification, then execute **M2 — Scene, Entity,
Tilemap, Animation and Collision** as one coherent pass. Produce a restartable world
demo through the existing engine, not a collection of disconnected interfaces.

## Start and scope

1. Read engineering rules, build plan, status, this file, architecture and ADR 0001;
   inspect the real implementation. Run canonical dev gates before changing code.
2. Exercise the native sandbox: WASD/arrows, diagonal speed, Q/E camera pan, Space,
   focus loss while holding a key, regain focus, minimize/restore, resize to different
   aspect ratios, close and Escape. Mark M1 complete only after these checks pass.
   If available, build/run the same tree using Windows x64 MSVC/Ninja and record results.
3. Add a small Scene/world ownership model with stable entity identifiers and explicit
   component/value composition. Avoid an archetype ECS or hidden global updates.
   Scene load/unload/restart must deterministically invalidate entity handles and clean up resources.
4. Extend sprite submission with validated texture source regions; build small reusable
   animation clips/playback and a tilemap representation/render path on that existing renderer.
   Prefer procedural/BMP test content. Introduce file formats/dependencies only if actually needed.
5. Add tested AABB overlap, static geometry, movement/response separated from detection,
   layers/masks and nonphysical trigger overlaps. Use a defined update phase and speed/size
   bounds; add a fixed step only if the real collision scenario needs it.
6. Add the minimum typed event/trigger contract needed to connect a trigger to a generic
   visible response, with safe subscriber lifetime during scene unload/restart.
7. Integrate an animated movable actor, tiled room, walls/static collision and a trigger
   that changes a visible object/state. Add collision/entity debug rectangles through
   the current queue and camera. Restart/unload/reload repeatedly without stale handles.
8. Extend meaningful unit/runtime tests for identifiers/invalidation, animation boundaries,
   tile/source-region geometry, collision/contact/trigger behavior and scene cleanup.
   Run Debug/Release/sanitizer gates, native checks, and refresh all persistent docs.

## Likely files and systems

- Existing public core/input/graphics API; `Renderer.cpp` for source regions.
- New used-only `engine/include/yk/{scene,animation,collision}` and matching private sources.
- Scene-owned resource bookkeeping above Renderer, respecting shared cached handle semantics.
- Sandbox demo, unit/integration tests, root CMake and documentation.

## Acceptance criteria

- A running room demo renders tiles and an animated actor through current texture/camera/queue code.
- Actor movement collides with walls; trigger overlap causes a visible response without blocking movement.
- Scene restart/unload/reload releases owned state without dangling entity/event/resource references.
- Collision and trigger behavior are deterministic at documented speeds and time steps.
- Debug visualization agrees with actual world geometry and camera projection.
- Canonical tests, format check and sanitizer checks pass; native runtime is exercised.
- Status/build plan/architecture/next pass accurately reflect results and remaining verification gaps.

## Risks and decisions to resolve through implementation

- Renderer release invalidates all aliases. Define scene asset ownership explicitly before
  unloading a shared BMP; do not silently add shared SDL texture ownership.
- Append-only texture slots are safe but metadata grows during repeated scene loads. If
  churn justifies recycling, use generation/renderer identity checks and prove stale-handle safety.
- Source-region clipping, anchor placement and animation dimensions need one authoritative policy.
- Variable-step collision must avoid tunneling at supported speeds; tests should decide
  whether a fixed-step update is warranted rather than introducing it speculatively.
- Event subscriptions must not outlive scene/subscriber ownership.
- The earlier concurrent runtime-test timeout was not reproduced in 40 reruns. Investigate
  if it recurs; do not broaden timeouts to hide a reproducible hang.
- Native keyboard/focus checks are pending because Computer Use could not access this new app.

## Explicitly deferred

Complete the reusable M2 world foundation before full platformer/cooperative game rules,
one-way/moving platforms, inventory, dialogue, quests, save/load, UI/audio, generalized
asset manifests/hot reload, packaging and CI. Do not add 3D systems, an editor, or a giant ECS.
