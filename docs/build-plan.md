# Proprietary 2D / 2.5D Engine — Build Plan

## 0. Project Goal

Build a small proprietary C++ game engine for two closely related game families:

1. **2D cooperative platformer**
   - Fireboy-and-Watergirl-style local co-op.
   - Two simultaneously controlled characters.
   - Platforming, gravity, jumping, moving platforms, hazards, switches, doors, character-specific interactions, level exits.

2. **Top-down / 2.5D castle exploration game**
   - Escapists-style exploration presentation.
   - Still fundamentally 2D: sprites, tilemaps, layers, Y-sorting, orthographic camera.
   - Top-down movement, NPCs, dialogue, items, inventory, quests, doors, containers, restricted areas, room transitions.

The engine is **not** intended to become a general-purpose Unreal/Unity replacement. It exists to build these games well, with a clean architecture that can grow only when real project needs demand it.

Primary target: **Windows x64**.
Secondary target: **macOS**, built natively from the same source tree rather than relying on a Windows translation layer.

---

# 1. Technical Direction

## 1.1 Language and build

- C++20 minimum.
- CMake as the canonical build system.
- SDL3 as the platform abstraction layer.
- Keep platform-specific code isolated behind engine-owned interfaces.
- Prefer vendored or CMake-managed dependencies that can build reproducibly.
- Avoid engine architecture that depends on Visual Studio project files directly.

## 1.2 Rendering model

The engine is a **2D renderer**.

Required capabilities:

- sprites
- sprite sheets
- animation
- tilemaps
- orthographic camera
- render layers
- world-space depth
- Y-sorting for top-down scenes
- text
- UI primitives
- simple particles later

Do **not** introduce a 3D renderer merely because the castle game is visually “2.5D”.

## 1.3 Physics/collision model

Build only the collision required by these games:

- AABB colliders
- static vs dynamic colliders
- trigger volumes
- collision layers/masks
- axis-separated collision resolution
- one-way platforms
- moving platforms
- hazard volumes
- interactable trigger areas

Do not build a general rigid-body simulation unless a shipped game requirement later proves it necessary.

## 1.4 Architecture principle

```text
Game-specific code
      ↓
Reusable engine systems
      ↓
Platform abstraction
      ↓
SDL3 / OS
```

The engine must never depend on either game.

The games may depend on the engine.

---

# 2. Repository Layout

Target structure:

```text
/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSES/
│
├── cmake/
│
├── docs/
│   ├── build-plan.md
│   ├── engineering-rules.md
│   ├── status.md
│   ├── whats-next.md
│   ├── architecture.md
│   ├── asset-format.md
│   └── decisions/
│
├── engine/
│   ├── include/<engine_name>/
│   └── src/
│       ├── core/
│       ├── platform/
│       ├── graphics/
│       ├── input/
│       ├── audio/
│       ├── assets/
│       ├── scene/
│       ├── collision/
│       ├── animation/
│       ├── ui/
│       └── debug/
│
├── games/
│   ├── platformer/
│   │   ├── src/
│   │   └── assets/
│   └── castle/
│       ├── src/
│       └── assets/
│
├── tools/
│
├── tests/
│   ├── unit/
│   └── integration/
│
├── assets/
│   └── shared/
│
└── third_party/
```

This is a target, not permission to create empty folders or placeholder files. Create structure only when used.

---

# 3. Persistent Project Memory

Every autonomous coding pass must assume the next session has **zero conversational memory**.

The repository is therefore the source of memory.

## 3.1 `docs/status.md`

Records the current factual state of the repository.

It must contain:

- current milestone
- what is implemented
- what compiles
- what runs
- tests/gates currently passing
- known defects
- architectural decisions already made
- important file locations
- dependency versions/approach
- platform status
- exact current limitations

Do not put speculative future plans here.

## 3.2 `docs/whats-next.md`

Records the recommended next execution pass.

It must contain:

- next objective
- concrete scope
- files/systems likely involved
- acceptance criteria
- risks/unknowns
- explicitly deferred work

This file should be rewritten at the end of every meaningful pass so a fresh agent can immediately continue.

## 3.3 `docs/build-plan.md`

This file is the long-term roadmap and project contract.

Do not rewrite completed history out of it. Mark milestones and acceptance criteria as they are completed.

## 3.4 Architecture Decision Records

Use `docs/decisions/NNNN-short-title.md` only for decisions that materially constrain future work.

Examples:

- renderer backend choice
- resource ownership model
- scene serialization format
- entity/component architecture
- dependency policy

Do not create ADRs for trivial implementation details.

---

# 4. Definition of Engine V1

Engine V1 is complete when both vertical slices can be built and run from the same engine codebase.

## 4.1 Shared engine systems

- application lifecycle
- deterministic main loop structure
- time / delta time
- logging and assertions
- input abstraction
- window lifecycle
- renderer
- textures
- sprites
- animation
- orthographic camera
- tilemaps
- render layers
- Y-sorting
- scene loading/unloading
- entities/components or equivalent lightweight composition model
- AABB collision
- triggers
- events/signals
- asset management
- text rendering
- basic UI
- audio playback
- save/load foundation
- debug rendering/overlay

## 4.2 Platformer vertical slice

A test level must demonstrate:

- Player A controlled by WASD.
- Player B controlled by arrow keys.
- Both active simultaneously.
- gravity
- jumping
- static platform collision
- at least one moving platform
- pressure plate or switch
- door controlled through an event/trigger relationship
- at least one character-specific interaction/hazard
- level exit requiring both players
- restart
- level-complete state

## 4.3 Castle vertical slice

A test area must demonstrate:

- top-down 4/8-direction movement
- walls/collision
- Y-sorted player/NPC/furniture rendering
- interaction prompt
- NPC dialogue
- item pickup
- inventory
- a quest/objective state
- a key or requirement-gated door
- room/scene transition
- simple save/load of player progress

---

# 5. Milestone Roadmap

The milestones are intentionally large. Each coding prompt should execute a substantial coherent pass rather than a tiny ticket.

---

## M0 — Repository Bootstrap and Engineering Baseline

**Status: COMPLETE** (2026-09-27, native macOS baseline; Windows validation pending).

### Goal

Turn an empty repository into a reproducible C++20/SDL3 engine workspace with verification infrastructure.

### Build

- root CMake project
- presets for supported development builds
- SDL3 dependency integration
- engine library target
- minimal executable target
- testing target/infrastructure
- warnings policy
- debug/release configuration policy
- formatting configuration
- static analysis hooks if practical
- logging/assertion foundation
- application bootstrap
- window creation
- clean shutdown
- platform detection macros only where necessary
- documentation baseline

### Acceptance

- [x] clean configure from repository root
- [x] clean build
- [x] executable opens a window and exits correctly
- [x] no engine/game dependency inversion
- [x] tests can be invoked from a documented command
- [x] `status.md` and `whats-next.md` accurately describe the repo

---

## M1 — Core Runtime + 2D Rendering Foundation

**Status: IN PROGRESS** — implementation integrated; hands-on desktop verification remains.

### Goal

Create a minimal reusable runtime capable of drawing and moving textured 2D objects.

### Build

- game loop
- timing
- input snapshot/state
- renderer ownership/lifetime
- texture loading
- sprite representation
- render queue
- transforms
- camera
- basic resource handles/ownership
- debug draw hooks
- test/demo scene

### Acceptance

- [x] sprite renders correctly (native Metal readback visually inspected)
- [ ] sprite can move from keyboard input (implemented; hands-on check pending)
- [x] camera transform works (unit and pixel tests)
- [x] resource lifetime is deterministic (release/recreation/failure-path tests)
- [ ] resizing behavior is defined and tested manually (automated resizing/pixel checks pass; hands-on check pending)
- [x] runtime shuts down without leaks/errors visible through available diagnostics (RAII review, native clean exit, ASan/UBSan; no full leak-detector claim)

---

## M2 — Scene, Entity, Tilemap, Animation, Collision

### Goal

Build the reusable world model needed by both games.

### Build

- lightweight entity/component composition
- scene lifecycle
- scene ownership
- tilemap data representation
- tile rendering
- collision map/static geometry
- AABB collider
- collision layers/masks
- trigger collider
- animation clips/state playback
- event/signal mechanism
- scene debug inspection

### Acceptance

- scene can create/destroy entities safely
- tilemap renders
- dynamic entity collides with static world
- trigger callback/event works
- animated sprite works
- scene unload releases resources correctly

---

## M3 — Platformer Vertical Slice

### Goal

Prove the engine can build the first game family.

### Build

- `PlatformController`
- gravity
- grounded detection
- jump buffering only if needed by actual feel testing
- one-way platforms if required
- moving platform support
- two simultaneous local input bindings
- hazards
- pressure plates/switches
- doors
- character-specific interaction filter
- cooperative exit condition
- restart flow
- level-complete flow

### Required demo

A single polished test room:

```text
A activates mechanism
→ B progresses
→ B activates mechanism
→ A progresses
→ both reach the exit
```

### Acceptance

- both characters are controllable simultaneously
- collision is stable at expected demo speeds
- puzzle relationship is data/event driven rather than hard-coded entity IDs in controller code
- restart reliably restores the initial state
- completing the room produces a level-complete state

---

## M4 — Top-Down / 2.5D Rendering and Interaction

### Goal

Prove the common engine supports Escapists-style world presentation.

### Build

- `TopDownController`
- normalized diagonal movement
- Y-sort key/depth policy
- world interaction detector
- `Interactable` contract
- doors
- pickups
- simple container/interactable object
- room transition trigger
- top-down test room

### Acceptance

- player moves correctly in 8 directions
- furniture/player/NPC overlap renders correctly by Y depth
- interaction target selection is deterministic
- door/pickup interactions work through shared interaction interfaces
- scene/room transition works

---

## M5 — Castle Gameplay Systems

### Goal

Add the minimum application systems for the castle game vertical slice.

### Build

- dialogue model and UI
- NPC interaction
- item definitions
- inventory model
- inventory UI
- quest/objective model
- requirement checks
- key-gated door
- simple save/load schema

### Acceptance

Playable flow:

```text
enter room
→ talk to NPC
→ receive/find objective
→ obtain item/key
→ inventory updates
→ gated door becomes usable
→ enter next room
→ save
→ reload and retain required progress
```

---

## M6 — Shared UI, Audio, Asset Pipeline, Debug Tools

### Goal

Make iteration practical without turning the project into an editor project.

### Build

- text/font abstraction
- basic UI layout primitives
- buttons/panels/images/labels
- pause flow
- audio playback abstraction
- music vs SFX channels/groups if needed
- asset manifest or registry
- missing-asset diagnostics
- hot reload only if simple and reliable
- debug overlay
- collision visualization
- entity identifiers/names in debug
- FPS/frame timing display

### Acceptance

- both demos use shared UI primitives
- audio lifecycle is stable
- missing assets fail clearly
- debug overlay can identify core scene/runtime state

---

## M7 — Data-Driven Content and Packaging

### Goal

Separate engine/game code from content enough that designers can iterate without recompiling every simple content edit.

### Build

- scene serialization format
- tilemap loading format
- animation definitions
- interactable configuration
- level/game config files
- asset manifest
- validation for malformed data
- Windows packaging
- macOS packaging

### Acceptance

- a representative level/room can be changed primarily through data/assets
- malformed content reports useful errors
- Windows distributable launches outside IDE
- macOS app build launches natively on supported hardware

---

## M8 — Hardening and Engine V1 Freeze

### Goal

Stabilize what exists instead of broadening scope.

### Work

- memory/resource audit
- ownership audit
- invalidation/lifetime audit
- collision edge cases
- save compatibility policy
- input edge cases
- scene transition edge cases
- frame pacing review
- debug/release behavior review
- warnings cleanup
- dependency/license inventory
- documentation cleanup
- CI if appropriate

### Acceptance

- all project gates pass
- both vertical slices run end-to-end
- no known P0/P1 issues
- documented limitations are explicit
- Engine V1 API surface is understandable from code and docs

---

# 6. Explicit Non-Goals for V1

Do not add these unless a real game requirement appears and the build plan is explicitly revised:

- 3D rendering
- skeletal 3D animation
- general rigid-body physics
- networking
- online multiplayer
- visual scripting
- node graph editors
- shader graph
- material editor
- custom programming language
- Vulkan backend solely for prestige
- DirectX 12 backend solely for prestige
- full scene editor
- built-in code editor
- navmesh system
- complex ECS designed for tens of thousands of entities
- proprietary image/audio formats
- advanced dynamic lighting engine
- plugin marketplace architecture

---

# 7. Large-Pass Execution Protocol

Every implementation prompt should request a **large coherent pass**.

A pass should normally include:

1. inspect repository and docs
2. validate current status against reality
3. execute the next milestone or substantial milestone section
4. integrate fully rather than leaving disconnected scaffolding
5. compile frequently
6. run automated tests
7. run available static checks
8. manually exercise the executable where possible
9. remove dead/temporary code
10. review architecture and ownership
11. update documentation
12. update `status.md`
13. rewrite `whats-next.md`
14. provide a concise completion report

The model should use its available context window aggressively. Prefer completing one large vertical tranche over producing many tiny follow-up prompts.

Do not intentionally stop after creating interfaces if the implementation can reasonably be completed in the same pass.

Do not leave TODO-driven pseudo-progress merely to keep a diff small.

---

# 8. Verification Gates

The exact commands may evolve with the repo. `status.md` must always contain the canonical current commands.

Target gates should eventually resemble:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Optional when configured:

```bash
cmake --build --preset dev --target format-check
cmake --build --preset dev --target lint
```

For release/package milestones, also build release presets and launch produced artifacts.

Never claim a milestone is complete if its required gates fail.

---

# 9. Coding Model Handoff Contract

At the beginning of every fresh coding session, the agent must read, in this order:

1. `docs/engineering-rules.md`
2. `docs/build-plan.md`
3. `docs/status.md`
4. `docs/whats-next.md`
5. relevant architecture/decision docs
6. relevant source files

The agent must treat the repository state as authoritative over assumptions in the prompt.

At the end of every meaningful coding session:

- update `docs/status.md`
- update `docs/whats-next.md`
- mark completed build-plan acceptance criteria/milestones where appropriate
- create/update ADRs only if a durable architectural decision was made
- document exact verification commands and their results

A future session must be able to answer these questions from the repository alone:

- What are we building?
- What is already implemented?
- What actually works?
- What is broken?
- What architectural choices are settled?
- What should be built next?
- How do I verify the current repo?

---

# 10. Progress Tracking

Use these milestone states:

```text
NOT STARTED
IN PROGRESS
BLOCKED
COMPLETE
```

Current state after the 2026-09-27 implementation pass:

| Milestone | Status |
|---|---|
| M0 Repository Bootstrap | COMPLETE |
| M1 Runtime + Rendering | IN PROGRESS |
| M2 Scene/Tilemap/Collision | NOT STARTED |
| M3 Platformer Vertical Slice | NOT STARTED |
| M4 Top-Down/2.5D | NOT STARTED |
| M5 Castle Gameplay Systems | NOT STARTED |
| M6 UI/Audio/Debug/Assets | NOT STARTED |
| M7 Data + Packaging | NOT STARTED |
| M8 Hardening / V1 Freeze | NOT STARTED |

---

# ▶ Resume Here

M0 is complete on the available macOS host. M1's runtime, textured sprites, input,
camera, ordering and resource ownership are implemented with passing Debug/Release/
sanitizer gates and native Metal readback. Hands-on keyboard/focus/minimize/resize
checks remain because the new app was inaccessible to Computer Use; Windows x64
build/runtime validation is also pending. See `docs/status.md` for exact evidence.

The next large pass should finish those available verification checks, then execute
**M2 — Scene, Entity, Tilemap, Animation and Collision** as a running integrated world
demo with safe restart/unload and meaningful tests. Follow `docs/whats-next.md` for
scope and acceptance. Do not reopen settled bootstrap architecture without a concrete reason.
