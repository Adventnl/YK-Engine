# What's Next

The editor, the runtime and the *Elemental Prototype* validation game work end to end on Linux
(exact evidence and limits: status.md; how the pieces fit: architecture.md). The product boundary is
unchanged: engine and editor first, the game only as proof. Keep engine, editor and game separate
(ADR 0004), keep reflection as the single source of component data (ADR 0005), and do not put
game rules in the engine or gameplay libraries.

Read engineering-rules.md, build-plan.md, status.md and architecture.md, and the docs of the
subsystem you touch. The list below is in the suggested order, each item with what "done" means.
Pick from a real need; do not scaffold ahead of one.

## 1. Prove it on real machines

Everything was verified on Linux with SDL's dummy video driver and the software renderer, which
exercises the real code paths but not a real GPU, window manager, mouse, HiDPI display or IME.
Windows and macOS were not built at all with the new code.

Done when: a CI workflow builds and runs CTest for dev/release on Linux, Windows (MSVC) and macOS;
someone has run the editor workflow by hand on each with a real display (including a HiDPI screen
and window resizing) and fixed what they found; status.md records the results per platform.

## 2. Let a game bring its own components

The editor and player host the modules compiled into them (`registerAllModules` in `game/`), so a
game with its own C++ components needs its own editor and player builds. There is no plugin
loading or scripting.

Done when: a second, tiny game module (kept in `tests/`) defines a component that shows up in
Add Component, saves, loads, plays and exports, using a documented CMake helper that builds the
editor and player for a game, with no engine change. Only after that is a plugin ABI or a scripting
language worth its cost; decide with an ADR.

## 3. Build levels faster

Levels are made of rectangles today; large levels are tedious and the sample has two small scenes.

- Tilemap component and paint tool (with collision generation).
- Polygon and edge colliders (`Collider` is box, circle, capsule); one-way platforms.
- Editing several selected entities at once in the Inspector (it shows the primary selection only).
- Grid size and snapping settings in the UI; align/distribute.
- Nested prefabs and prefab overrides (placed prefab copies are independent by design today).

Done when: a level of a few hundred entities is authored from scratch in the editor by script,
saved, played and validated, with each item covered by core tests and a UI script.

## 4. Assets and animation authoring

Assets are copied into the project folder by hand (PNG/BMP images, WAV sounds), and `SpriteAnimator`
clips are edited as numbers. Add an import step with previews, sprite-sheet slicing, an animation
clip editor and reload on file change, so replacing the placeholder shapes with real art is a
drag-and-drop job. Gameplay must keep working with placeholders.

## 5. Runtime polish

- Render-pose interpolation between fixed ticks (`physics::World::interpolatedPose` exists; the
  runtime writes back the latest tick), so motion stays smooth above 60 Hz displays.
- Shape casts, gamepad input and key rebinding, music and a mixer, and a menu/settings UI kit for
  finished games (the prototype has a HUD only).

## 6. Editor robustness

- Autosave and crash recovery (a session that dies loses unsaved edits today).
- Snapshot undo costs about 9 ms per committed edit at 500 entities and 0.24 s at 10,000 (ADR 0006).
  Reuse the previous snapshot first (halves it), per-entity deltas only if scenes that large appear.
- Optional live tweaking of values during Play that can be kept or discarded on Stop.

## Standing checks

Verification stays: configure/build/CTest for dev, release, headless and asan, plus format-check,
`git diff --check`, and for anything visible a UI script under `tests/editor/` or real pixel
readback. Record exact platform, toolchain and results in status.md and update these docs in the
same change.
