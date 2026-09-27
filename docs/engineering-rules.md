# Engineering Rules

Production-quality, senior-engineer code: **correct, maintainable, modular, performant, and simple**. Optimize for correctness, clear ownership, reliable runtime behavior, and long-term maintainability—not diff size, abstraction count, framework cleverness, or feature count.

This repository is a small proprietary C++ game engine and its game modules. It is **not** a general-purpose engine project unless the build plan is explicitly changed.

---

## 1. Before Coding

1. Read `docs/build-plan.md`, `docs/status.md`, and `docs/whats-next.md`.
2. Understand the request and existing architecture before editing.
3. Inspect relevant source, tests, CMake, dependencies, and project rules.
4. Search before creating anything new.
5. Identify the **smallest architecture that correctly supports the requested behavior**, but execute a large coherent implementation pass when requested.
6. Define verifiable success criteria before implementation.
7. Resolve ambiguity from existing code/docs where possible; ask only when genuinely blocked.
8. Verify library/compiler/platform behavior when uncertain.
9. Prefer proving assumptions with a compile, test, minimal executable, or upstream documentation rather than guessing.

Do not redesign stable architecture without a concrete reason.

Do not create general-purpose abstractions merely because a game engine could theoretically need them later.

---

## 2. Change Discipline

- Make cohesive, behaviorally complete changes.
- Reuse existing engine systems before creating parallel systems.
- One source of truth; no duplicate implementations.
- Remove obsolete code created or superseded by the change.
- No speculative features, placeholders, fake backends, invented APIs, or dead scaffolding presented as progress.
- Do not restructure unrelated code.
- Every module/file must have a clear responsibility.
- Do not churn code that already meets the standard.
- Large passes are encouraged, but each pass must remain conceptually coherent.
- A large diff is acceptable when it completes a real milestone cleanly.
- Never use “future cleanup” as an excuse for knowingly broken ownership, unsafe lifetime, or invalid architecture now.

### Three-Pass Rule

For meaningful work:

1. Implement the complete intended slice.
2. Self-review and fix correctness, ownership, API, and architecture weaknesses.
3. Review again as a strict senior engine engineer, then run verification.

Do not narrate these passes in code comments or user-facing completion notes.

---

## 3. Architecture

Canonical dependency direction:

```text
games/*
  ↓
engine public API
  ↓
engine subsystems
  ↓
platform abstraction
  ↓
SDL3 / OS / approved third-party libraries
```

The engine must never depend on game-specific code.

Preferred engine subsystem direction:

```text
core
↑
platform / input / assets
↑
graphics / audio
↑
scene / animation / collision / ui
↑
game modules
```

Exact dependency edges may vary, but circular subsystem dependencies are not acceptable.

### Structure

Use the repository layout described in `docs/build-plan.md`.

Rules:

- No second home for engine systems.
- No duplicate “utility” implementations scattered across games.
- Public engine headers belong under the engine's public include path.
- Private implementation details remain private.
- Game-specific components remain inside the corresponding game module.
- Platform-specific code is isolated.
- Avoid giant `Common.h`, `Utils.h`, or catch-all modules.
- Avoid barrel-style headers that indiscriminately include the entire engine.
- Fix misplaced code instead of adding dependency exceptions.
- Prefer narrow interfaces between subsystems.

### Engine vs Game Boundary

Engine owns reusable mechanics/infrastructure such as:

- application lifecycle
- rendering
- input abstraction
- resources/assets
- scene lifecycle
- generic entity composition
- generic animation
- generic collision
- generic triggers/events
- generic UI/audio primitives

Game modules own rules such as:

- platformer character behavior
- castle NPC behaviors
- quest semantics
- specific puzzle rules
- specific item definitions
- game-specific win/fail conditions

If only one game needs something and there is no proven reusable abstraction, implement it in that game first.

---

## 4. Ownership, Lifetime, and Memory

C++ lifetime bugs are P0/P1 issues, not polish.

Rules:

- Every owning relationship must be obvious from the type/API.
- Prefer RAII.
- Prefer values and `std::unique_ptr` for exclusive ownership.
- Use `std::shared_ptr` only when shared ownership is genuinely required and documented by the design.
- Raw pointers/references may be non-owning; make lifetime expectations obvious.
- Do not manually `new`/`delete` in ordinary engine code when RAII can express ownership.
- Resource handles must have defined invalidation/lifetime semantics.
- Do not return references/pointers to storage that can be silently invalidated without a clear contract.
- Scene unload/destruction order must be deterministic.
- SDL and third-party resources must be released through safe wrappers.
- No use-after-free, double-destruction, dangling callback, or listener lifetime ambiguity.

When callbacks/events can outlive their source or subscriber, the lifetime model must be explicit and tested.

---

## 5. Core Runtime and Game Loop

The main loop must have explicit phases.

A typical structure may resemble:

```text
poll platform events
→ update input state
→ update simulation/game state
→ resolve collision / gameplay systems
→ update animation
→ build render work
→ render/present
```

Rules:

- Do not hide uncontrolled global update order across arbitrary objects.
- Time units must be explicit.
- Delta-time handling must be consistent.
- Clamp or otherwise handle pathological frame delays where appropriate.
- Fixed-step simulation may be introduced only where a real system needs it.
- Do not mix rendering side effects into pure gameplay state updates without a concrete reason.
- Pause behavior must be deliberate.
- Shutdown must be orderly and idempotent where practical.

---

## 6. Entities and Components

Keep entity architecture lightweight.

The project does not need a high-scale archetype ECS unless profiling proves otherwise.

Components must:

- have one primary responsibility
- expose a narrow API
- avoid implicit global dependencies
- make ownership/lifetime clear
- be testable where meaningful

Components must not:

- become service locators
- silently fetch arbitrary global systems
- duplicate engine services
- contain unrelated gameplay workflows
- exist solely to satisfy an architectural pattern

Prefer composition for actual reusable behavior, not component proliferation.

---

## 7. Rendering

The engine is 2D-first.

Required principles:

- orthographic world rendering
- explicit render ordering
- deterministic layer/depth rules
- Y-sorting as a 2D depth technique for top-down scenes
- no hidden dependency on 3D transforms/perspective
- batch/optimize only when justified by profiling or obvious scale

Rendering code must separate:

```text
world/game state
↓
renderable data / render commands
↓
renderer backend
↓
SDL/GPU API
```

Rules:

- Do not make game logic depend on draw order side effects.
- Texture/resource lifetime must be centralized.
- Missing assets must fail visibly with useful diagnostics.
- Images must preserve intended dimensions/aspect unless explicitly transformed.
- Camera/world/screen coordinate conversion must have one authoritative implementation.
- Keep render-state mutation controlled.

Do not add 3D rendering systems for the castle game. Its visual depth is implemented with sprites, layers, tilemaps, and Y-sorting unless the build plan is deliberately revised.

---

## 8. Collision and Physics

Build the physics the games need, not a physics engine for its own sake.

Canonical initial collision scope:

- AABB
- static world collision
- dynamic actor movement
- triggers
- layers/masks
- one-way platforms where needed
- moving platforms where needed

Rules:

- Collision detection and response are separate concepts.
- Trigger overlaps must not accidentally perform physical response.
- Axis-separated platformer resolution must be deterministic.
- Avoid tunneling at supported gameplay speeds; document speed/size constraints where relevant.
- Grounded state must come from defined collision/contact logic, not animation state.
- Character-specific hazards/interactions should use tags/layers/data, not duplicated collision engines.
- Collision debug rendering must be available once collision work becomes substantial.

Do not add rotations, impulses, angular velocity, joints, or continuous rigid-body simulation unless a shipped mechanic requires them.

---

## 9. Input

Input is a reusable engine service.

It must distinguish where appropriate:

```text
held
pressed this frame
released this frame
```

Rules:

- Game code should bind actions, not scatter raw SDL scancodes throughout gameplay logic.
- Two local players must be able to bind separate controls simultaneously.
- Input state must update once per frame in a defined phase.
- Focus loss/window events must not leave keys permanently stuck.
- Controller support may be added later without redesigning every gameplay component.
- Do not hard-code platform-specific key assumptions outside the input layer.

---

## 10. Assets and Data

Assets must have a single loading/ownership path.

Prefer:

```text
asset identifier
↓
asset registry/manager
↓
loaded resource handle
```

Rules:

- Do not load the same texture independently from arbitrary gameplay objects.
- Avoid filesystem paths scattered through game logic.
- Asset loading errors must identify the requested asset and source path/config.
- Define relative-path behavior from a stable project/runtime root.
- Data formats must be versionable or at least evolution-aware once save/content compatibility matters.
- Parse external data at boundaries and validate it before using it as domain state.
- Keep malformed-data errors actionable.

Do not create a proprietary binary package format until ordinary files/manifests become a demonstrated problem.

---

## 11. Events and Interactions

Use events/signals for decoupled relationships such as:

```text
pressure plate
→ event
→ door
```

Rules:

- Events are not a substitute for every direct function call.
- Event names/IDs must be stable and validated when data-driven.
- Avoid invisible global event spaghetti.
- Subscriber lifetime must be safe.
- Prefer typed events in code.
- Data-driven links should fail clearly when a target/event is invalid.

Generic interaction contracts may support doors, switches, NPCs, pickups, chests, exits, and similar objects without forcing their game-specific behavior into the engine core.

---

## 12. State and Save Data

Keep state minimal and authoritative.

- One source of truth for each runtime state.
- Derive values instead of duplicating them.
- Avoid bidirectional synchronization between redundant representations.
- Scene-local state and persistent game state must be deliberately separated.
- Save data must not serialize raw pointers, transient handles, or implementation addresses.
- Save schema changes must be considered once save/load is introduced.
- Failed loads must produce explicit errors and safe fallback behavior.

For asynchronous work, if introduced later, explicitly handle:

- cancellation
- stale completion
- shutdown
- ownership
- error propagation

Never swallow errors.

---

## 13. Errors, Logging, and Assertions

Use the right mechanism:

- expected recoverable failure → result/error return
- programmer invariant violation → assertion in development
- important runtime state/failure → structured logging
- unrecoverable initialization failure → clear fatal error and orderly shutdown where possible

Rules:

- No silent catches.
- No `std::cout` debugging left scattered through production code.
- Error messages should include relevant context.
- Do not use exceptions as unstructured control flow unless the project explicitly adopts an exception policy.
- Third-party error codes/messages should be adapted into engine diagnostics.

---

## 14. Types and API Boundaries

- Explicit types at subsystem boundaries.
- Prefer scoped enums.
- Prefer strong domain types when confusion would be costly.
- Avoid boolean-parameter soup.
- Keep nullability/optional state explicit.
- Never expose mutable internal containers unnecessarily.
- Prefer `std::span`, views, const references, handles, or narrow query APIs where suitable.
- Avoid macros for ordinary language features.
- Avoid magic integer IDs without a type/contract.
- Keep public APIs smaller than private implementation surfaces.

Engine headers are a product surface. Treat changes to them deliberately.

---

## 15. C++ Style and Safety

- C++20.
- RAII by default.
- `const` correctness where meaningful.
- `enum class` over unscoped enums.
- Prefer standard library facilities over custom containers/utilities unless a demonstrated requirement exists.
- No C-style casts.
- Avoid undefined behavior and implementation-dependent assumptions.
- Avoid unnecessary heap allocation in per-frame hot paths.
- Do not prematurely replace clear standard code with custom allocators, intrusive containers, or template metaprogramming.
- Keep templates limited to cases that clearly benefit from compile-time generic behavior.
- Header implementation should not explode compile times without reason.

Warnings from project code are treated seriously. New warnings require justification or correction.

---

## 16. Performance

Optimize **measured or obvious bottlenecks**, not code for appearance.

Check especially:

- per-frame allocations
- texture/state churn
- unnecessary resource reloads
- O(n²) collision/query loops at meaningful scales
- repeated filesystem access
- expensive string work in hot loops
- large copies
- avoidable cache-unfriendly object ownership
- redundant sorting
- off-screen work

Use when justified:

```text
reserve/capacity planning
object reuse
spatial partitioning
batching
caching
handles/IDs
small-vector-like optimization only if measured
```

Do not add custom allocators, job systems, lock-free structures, SIMD rewrites, or elaborate ECS storage merely because they are common in large engines.

Always preserve correctness before optimization.

---

## 17. Threading

Default to single-threaded engine/game logic for V1 unless a concrete bottleneck requires otherwise.

If threading is introduced:

- ownership across threads must be explicit
- synchronization contracts must be documented
- renderer/platform thread-affinity rules must be respected
- no detached background threads with ambiguous shutdown
- jobs must be joined/cancelled during shutdown
- races are P0/P1 issues

Do not introduce a job system speculatively.

---

## 18. Platform Boundaries

Primary platform: Windows x64.
Secondary platform: macOS.

Rules:

- Prefer SDL3/platform-neutral code for common functionality.
- Platform-specific implementation lives behind narrow engine interfaces.
- Do not put `#ifdef _WIN32` throughout gameplay code.
- Build configuration differences must be documented in CMake/presets/docs.
- File paths must use portable handling.
- Do not rely on Windows-only runtime behavior unless explicitly isolated and documented.
- macOS should be built natively from source; translation layers are not the engine architecture.

---

## 19. Dependencies

Each dependency must earn its place.

Before adding one, check:

1. Is the capability truly needed now?
2. Is implementing the small required subset ourselves actually simpler?
3. Is the dependency maintained and compatible with Windows/macOS?
4. Is its license compatible with distribution?
5. Can it be integrated reproducibly through CMake?
6. Does it create undesirable transitive weight or architectural coupling?

Rules:

- SDL3 is the platform foundation unless deliberately changed.
- Do not add a full game engine/framework beneath the proprietary engine.
- Record third-party licenses.
- Pin/record versions or revisions in a reproducible way.
- Do not silently upgrade dependencies in unrelated changes.

---

## 20. Security and External Input

Even a local game engine must treat external data carefully.

- Never hardcode secrets/tokens.
- Validate parsed files and serialized data.
- Check bounds before indexing external/data-driven arrays.
- Avoid unsafe string formatting and raw buffer manipulation.
- Do not trust asset metadata blindly.
- Save files must fail safely when malformed.
- File operations must not unintentionally escape intended roots when processing data-driven relative paths.

---

## 21. Testing and Verification

Logic changes require tests where practical, especially:

```text
math/transforms
resource identifiers
animation frame selection
collision overlap/resolution
trigger semantics
input transitions
scene lifecycle
serialization/parsing
inventory/quest state
save/load
edge cases
error cases
```

Not every render result needs a unit test. Use the correct verification method.

### Required gates

The exact commands live in `docs/status.md` and must remain current.

Expected baseline:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

When configured, also run formatting/static-analysis targets.

Never claim success when a required gate fails. Report the exact command and result.

### Runtime Verification

For runtime-affecting work:

```text
build
→ launch
→ exercise intended scenario
→ inspect logs/debug output
→ test failure/edge path where relevant
→ fix largest issue
→ repeat
```

For graphics/collision work, use debug visualization when available.

Manual verification is mandatory for behavior that automated tests cannot prove.

---

## 22. Code Review

Prioritize:

```text
correctness
→ lifetime/ownership
→ architecture/dependency direction
→ runtime determinism/state
→ error handling
→ data boundaries
→ platform portability
→ performance
→ maintainability
→ polish
```

Severity:

```text
P0  crashes, memory corruption, security, destructive data loss, severe races
P1  ownership/lifetime hazards, architecture inversion, incorrect gameplay/runtime state, broken platform/build path
P2  duplication, avoidable coupling, poor API boundaries, magic values, dead code, meaningful performance waste
P3  naming, documentation, minor polish
```

Every review finding must identify:

```text
path/line or symbol
→ problem
→ why it matters
→ specific fix
```

Do not report vague quality concerns.

---

## 23. Refactoring

Refactors preserve behavior unless a bug fix/behavior change is explicitly part of the task.

For structural moves:

```text
map old path → new path
→ identify dependency effects
→ move cleanly
→ update includes/CMake/tests/docs
→ remove compatibility debris where unnecessary
→ run gates
```

Do not combine unrelated refactors merely because the pass is large.

Large passes should be broad in completed capability, not random in scope.

---

## 24. Documentation as Persistent Memory

Assume every future prompt begins with no useful chat memory.

For every meaningful implementation pass:

- read `docs/build-plan.md`
- read `docs/status.md`
- read `docs/whats-next.md`
- update `docs/status.md` to factual current reality
- rewrite `docs/whats-next.md` with the next coherent pass
- update the build-plan milestone status/acceptance where appropriate
- add/update architecture docs when public behavior changes
- add an ADR when a durable architectural decision needs to survive future sessions

Documentation must describe the repository that actually exists, not the intended repository.

Do not leave stale “next” instructions after completing them.

---

## 25. Finish Standard

Before reporting completion:

```text
inspected
→ implemented
→ integrated
→ self-reviewed
→ built
→ tested
→ manually exercised where relevant
→ warnings/errors reviewed
→ documentation updated
→ status updated
→ next pass defined
→ limitations stated
→ no unrelated speculative systems added
```

Final check:

```text
correct ownership?
correct dependency direction?
engine vs game responsibility correct?
one source of truth?
portable path?
resource lifetime safe?
collision/state deterministic?
no duplicate system?
no speculative generalization?
all required gates passed?
docs match reality?
next session can continue without chat memory?
```

### Completion Report

Keep it concise and factual:

```text
changed
why
important files
verification commands/results
known limitations
next milestone/pass
```

**Never say “done” unless the required verification gates for the claimed scope pass.**
