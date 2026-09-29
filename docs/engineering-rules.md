# Engineering Rules

This repository holds a 2D engine, its editor, a standalone player and one prototype game that
validates them. Keep those parts separate (see architecture.md) and keep the repository honest:
what the docs claim must be what the code and tests do.

Before coding read this file, build-plan.md, status.md, whats-next.md, architecture.md and the
documentation of the subsystem you touch. Repository reality overrides old claims; when you find a
claim that is no longer true, fix the document in the same change.

## Layout and boundaries

- Public engine headers in `include/yk/`, implementation in `src/`, game module in `game/`, player in
  `player/`, editor in `editor/core` (no SDL, no UI toolkit) and `editor/ui` (Dear ImGui), tests in
  `tests/`, docs in `docs/`, notices in `LICENSES/`, generated output in ignored `build/`.
- Dependencies point one way: game module -> gameplay -> engine; editor and player -> engine +
  the game modules they host. The engine and gameplay libraries never include editor or game headers
  and contain no rules of any particular game. Game rules go in a game module; reusable mechanics go
  in `yk::gameplay`; a capability every game needs goes in the engine.
- The engine has no global service locators, mutable singleton worlds or circular dependencies.
  Prefer values, RAII, unique ownership and explicit non-owning handles. Handles must reject
  destroyed objects, recycled slots and foreign owners.
- Physics has no window, renderer or scene dependency and Box2D stays private. Public headers
  contain no Box2D or SDL types.
- Everything runs on the main thread; thread affinity is explicit and asserted. Do not add a job
  system without a measured requirement.
- `YK_RUNTIME=OFF` must keep building everything that needs no window: engine, gameplay, game
  module, editor core and their tests.
- Use narrow interfaces and real module seams; no catch-all helpers, forwarding scaffolding,
  speculative interfaces, empty folders or TODO-driven scaffolding.

## Data and reflection

- Editable state is public component data declared once through the registry. Serialization,
  inspector widgets, prefabs, undo, copy/paste, generated docs and validation all read that
  declaration. Do not write editor code or file-format code for an individual component.
- Runtime-only state is declared `readOnly` (shown, never saved). `onAdd` defaults must never be the
  only place a saved value could come from.
- Scene files reference entities by id and assets by project-relative path. Loaders validate and
  name the failing entity/component/property; they never return partial data.
- `docs/components.md` is generated (`yk_player --components`); a test fails when it is stale.
- Placeholder art policy: prototype and test content uses simple colored shapes and procedural
  tones so real assets can replace them later by assigning a texture or sound. Gameplay never
  depends on how something looks; visuals and gameplay logic stay separate.

## Editor rules

- Everything the editor does to a scene lives in `editor/core` and is unit tested without a window.
  UI code calls it and draws; it never edits a scene directly.
- Every scene modification is a change on an `EditorDocument` (`beginChange`/`endChange`, `change`,
  or a ready-made edit) so it is undoable and dirty tracking stays right. Between frames hold
  `EntityId`s, never `Entity*` or `Component*` (undo replaces the scene).
- Play mode runs a copy; it must never modify the edited document.
- Behavior that a person can see or click needs a UI script under `tests/editor/` (CTest runs it
  headlessly through real input). New widgets that a script may need register a name with
  `markItem`.

## Correctness and physics

- Physics uses meters, kilograms, seconds and radians; rendering uses world units and degrees.
- Fixed simulation ticks are separate from frame time; report clamped/dropped time.
- Validate finite inputs, geometry, materials, handle ownership and timing at API boundaries.
  Detection, physical response and nonphysical trigger events have distinct contracts.
- Preserve events from every simulated tick. Destruction events may carry invalid historical handles.
- No callback may mutate a locked solver world. Application callbacks remain synchronous.
- Document CCD and sensor limitations. Do not claim tunneling prevention or bitwise determinism.
- Reject expected misuse through Result/Status; assertions enforce programmer invariants. Do not
  swallow failures. No raw owning new/delete outside tightly controlled private factories.

## Build, tests and dependencies

- Project warnings are errors (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`, or
  `/W4 /WX`). Dependencies (Box2D, SDL3, Dear ImGui, stb_image) are pinned by hash or commit;
  redistributed notices live in `LICENSES/` and `THIRD_PARTY.md`.
- Tests verify behavior, lifecycle, invalid input and regression cases; fix the cause of a failing
  test rather than weakening the assertion. Gameplay changes need a headless simulation; rendering
  changes need real pixel/readback evidence; level content needs the bot playthrough
  (`prototype_tests`), which fails when the sample level cannot be completed.
- Gates before handing off: configure, build and CTest for `dev`; Release, `asan` and `headless`
  for changes to shared code; `format-check`; `git diff --check`. Headers and macros also get a
  build with the other compiler (`-DCMAKE_CXX_COMPILER=clang++`), which found GNU-only macro use
  and `&` on booleans that GCC accepted.
- What gets installed is tested (`install`): exactly the two programs, the sample project, docs and
  notices; no headers or libraries of the private dependencies; the installed player and editor run
  the installed sample; CPack makes an archive. Change install rules and that test together.
- Keep builds green in cohesive units and commit with clear messages.

## Handoff

After meaningful changes update status.md, whats-next.md and any affected architecture, editor or API
docs. Record exact verification results, platform limits and known omissions. Add an ADR
(`docs/decisions/`) for durable architecture choices. Do not claim completion over a failing gate.
