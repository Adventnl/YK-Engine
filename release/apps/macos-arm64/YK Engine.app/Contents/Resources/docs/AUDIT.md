# Audit of the existing engine (start of the "playable engine" pass)

This is the audit that preceded the work described in `STATUS.md`. It records what was found by
reading the code, building it and running it — not what the older documents claimed. It is kept so
that later readers can see why each decision below was taken.

## Headline

The task brief described the repository as "a very early/basic version of the engine". It was not.
It was a disciplined, reasonably complete engine: about 24,000 lines of C++20, reflection-driven
components, a headless fixed-step runtime on Box2D 3.1.1, an SDL3 renderer, a Dear ImGui editor with
undo, gizmos and Play mode, a standalone player, 17 CTest suites and an install/CPack step.
A clean Debug build takes about 80 s on 4 cores and all 17 tests passed before any change was made.

So the guiding rule of this pass was the one in the brief: **keep what works, replace what does not
serve the goal, and do not rewrite for its own sake.** The gaps were about *what a real game needs
on top of that foundation* and about *the editor's look and workflow*, not about the foundation.

## How the audit was done

1. Read every public header and the implementations of scene, runtime, physics binding, gameplay,
   renderer, input, animation, assets and the editor (core and UI).
2. Built and ran the complete baseline (`dev` preset, GCC 13.3): 17/17 tests passed.
3. Looked at the editor and the sample game through the screenshots the UI tests write.
4. Grepped engine, gameplay and editor code for game-specific names (fire, water, lava, ...).
5. Read the Box2D 3.1.1 source where a feature (one-way platforms) depends on its behaviour.

## What is good and stays

| Area | Finding | Decision |
|---|---|---|
| Entity / component / reflection model | Components are plain classes, each field declared once (`field("name", &T::member)`); that one declaration drives JSON, inspector, prefabs, undo, copy/paste, docs and validation. Unknown saved properties warn instead of failing, so schemas can evolve. | **Keep.** New components are added through it. |
| Scene, prefab and project files | Validated loaders that name the failing entity/component/property, 64-bit random entity ids (merge friendly), project-relative paths that reject `..` and absolute paths. | **Keep**, extend (input map, build settings, texture metadata). |
| Runtime | Headless `GameRuntime`, fixed 60 Hz ticks with bounded catch-up, deferred destroy/spawn, event bus, blackboard, restart. Same code runs in the player, the editor's Play and the tests. | **Keep.** |
| Physics | Lifetime-safe handle API over Box2D, contact/sensor events, CCD, queries, tested. | **Keep**, add one-way platforms and a wedge shape. |
| Gameplay library | Signals (sources → receivers), plates, levers, doors, moving platforms, hazards with tag filters, collectibles, checkpoints, goals, killable, platformer controller with coyote time, jump buffer, slopes, platform riding. | **Keep**, extend. |
| Editor core (`editor/core`) | Document with snapshot undo, selection, picking, gizmo state machine, project I/O, play session — all UI-free and unit tested. | **Keep.** |
| Scripted UI driver + tests | Real SDL events injected into the real editor with fixed time steps; screenshots on failure. | **Keep**, rewrite scripts for the new layout. |
| Engine/game separation | A grep of `include/`, `src/`, `editor/`, `player/` for fire/water/lava/etc. found nothing game specific. The only leak is the editor welcome screen's hard-coded sample-project path. | **Keep**, remove the leak. |

## What did not meet the brief

| # | Finding (evidence) | Consequence | Decision |
|---|---|---|---|
| 1 | **Editor look.** Blue-slate palette (`28,30,37` background, blue selection), Dear ImGui's default pixel font at 15 px, free-floating docked windows with tab titles, a toolbar. Screenshots confirm it. No activity bar, editor tabs, breadcrumbs, status-bar states or split panes in the VS Code sense. | Does not read as a professional development tool. | **Replace** the theme, fonts, icons and the whole shell layout (docking → fixed workbench with sashes). Keep the panel *content* logic. |
| 2 | **Input is hard-coded to keys.** `PlatformerController` has `leftKey/rightKey/jumpKey`, `LevelFlow` has `restartKey`. `ActionBinding` exists but nothing uses it. No player profiles, no rebinding, no path to gamepads. | Violates "do not hardcode gameplay around key codes". | **Replace** with named actions, bindings grouped in per-player action sets stored in the project, and a `PlayerInput` component. |
| 3 | **Animation is a clip player.** `Animator` plays contiguous frame ranges at one fps; there is no state machine. `PlatformerController` picks clip names (`"idle"`, `"run"`, `"jump"`, `"fall"`) itself and writes `flipX` itself. `Killable` just hides the sprite. | Exactly the "one giant hardcoded character animation function" the brief forbids; no land/interact/death. | **Extend** clips (explicit frame lists, per-frame timing, events, follow-up clip) and **add** an animator controller (parameters, states, transitions with exit time) driven by data. Gameplay publishes generic parameters only. |
| 4 | **Art pipeline is minimal.** Sprites are coloured rectangles/ellipses or a texture stretched over the sprite size. Resizing a textured platform distorts it; there is no tiling or 9-slice, no per-texture filtering or scale, no parallax, no blend modes. | A textured, resizable platform prefab (the core level-building idea in the brief) is impossible. | **Extend** `SpriteRenderer` (Simple/Tiled/Sliced, blend, parallax) and add texture metadata sidecars. |
| 5 | **No view culling; hierarchy rebuilt repeatedly.** `SceneRenderer::drawWorld` submits every sprite every frame; `Scene::hierarchyOrder()` allocates and walks the tree on each call and is called several times per tick and per frame; `findByName` rebuilds it per call. Fine at 47 entities. | Large levels degrade. | **Fix**: cull to the camera view, cache the hierarchy order by scene revision, measure with a stress test. |
| 6 | **Physics gaps.** Collider shapes are box/circle/capsule. One-way platforms are documented as "not exposed"; no pre-solve hook; slopes only by rotating boxes. | No jump-through platforms or ramps. | **Extend**: pre-solve based one-way platforms and a wedge (ramp) shape. |
| 7 | **No effects.** No particle emitter, glow or procedural motion component; decoration cannot move. | Levels look dead. | **Add** generic `ParticleEmitter`, `Light2D`, `Oscillator`. |
| 8 | **Interaction gaps.** Levers work by touch only (no Interact action); collectibles do not count totals for a HUD; goals, collectibles and checkpoints raise no events an effect could react to. | Needed for the brief's Interact action and readable HUD. | **Extend** the gameplay library. |
| 9 | **The demo game is compiled into the tools.** `registerAllModules` links `game/` (the prototype module) into both `yk_editor` and `yk_player`; the sample project lives in `projects/` and is built by a C++ generator (`make_prototype`). A game with its own components would need its own editor build; there is no documented way to do it. | Weak separation between "engine" and "game", and the level is produced by code rather than authored. | **Restructure**: a separate demo project folder that is pure data; game rules that any puzzle platformer needs move into the gameplay library; a documented CMake helper builds a tool host for a game that *does* need C++ components, verified by a tiny example module. |
| 10 | **Placeholder-only visuals.** The sample is coloured rectangles. | Does not look like a game. | **New** original generated art (sprite sheets, tiles, decoration, hazards, backgrounds) and a proper level. |
| 11 | **Export is same-OS only.** `Export Game` copies `yk_player` beside a project copy of the host platform; no target abstraction, no build settings, no Windows or macOS result, no CI. | "Packageable as a standalone desktop application" is not demonstrable. | **Add** a `BuildTarget` abstraction and project build settings; runtime templates per target; cross-compile and run the Windows player to verify what can be verified. |
| 12 | **Editor gaps.** Inspector edits the primary selection only; hierarchy has no visibility toggle; asset browser has no thumbnails or import settings; console has no timestamps or grouping; no problems/build/profiler views. | Falls short of the brief's editor list. | **Extend** as part of the editor rework. |
| 13 | **Docs describe the previous state and use lowercase names.** | Stale after this pass. | **Rewrite** as `ARCHITECTURE`, `BUILDING`, `EDITOR`, `PROJECT_FORMAT`, `STATUS`, keep the verified-status discipline. |

## What was removed

* `game/tools/make_prototype.cpp` and the C++-built sample level (the level is authored data now).
* `SpriteAnimator` as a name (it becomes `AnimatedSprite`, keeping clip playback and adding the state machine).
* Per-key fields on `PlatformerController` and `LevelFlow`.

## Things deliberately not done

* **No renderer rewrite.** The SDL_Renderer based `Renderer` is adequate for 2D at this scale; batching
  is done by SDL. A custom GPU backend would be a rewrite without a measured need.
* **No scripting language in this pass.** A behaviour today is a C++ `Component`; games that need
  their own get a module (see `ARCHITECTURE.md`). An embedded language is the highest-value follow-up
  and is described in `STATUS.md`; it needs dynamic (per-instance) reflection for exposed script
  variables, which is a cross-cutting change that should not be rushed.
