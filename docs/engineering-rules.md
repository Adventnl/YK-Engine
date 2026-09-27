# Engineering Rules

This repository is the core 2D engine library. Do not add games, gameplay rules,
a sandbox, game-specific assets, or application executables. Engine tests are allowed.
All project directories belong inside `engine/`; root files provide build entry points.

Before coding read this file, build-plan.md, status.md, whats-next.md, architecture.md
and the relevant subsystem documentation. Repository reality overrides old claims.

## Architecture and ownership

- C++20, CMake, Windows x64 primary, native macOS secondary.
- Public headers: engine/include/yk; implementations: engine/src.
- Physics depends on core math/results and private Box2D. It must work without SDL.
- Platform/graphics depend on SDL privately. Physics debug rendering is a graphics adapter.
- No game dependencies, global service locators, mutable singleton worlds or circular dependencies.
- Prefer values, RAII, unique ownership and explicit non-owning handles.
- Handles must reject destroyed objects, recycled slots and foreign owners.
- Thread affinity is explicit. Do not add a job system without a measured requirement.
- Use narrow interfaces and real module seams. Avoid catch-all helpers and forwarding scaffolding.

## Correctness and physics

- Physics uses meters, kilograms, seconds and radians; rendering uses explicit world units/degrees.
- Fixed simulation ticks are separate from frame time. Report clamped/dropped simulation time.
- Validate finite inputs, geometry, materials, handle ownership and timing at API boundaries.
- Detection, physical response and nonphysical sensor events have distinct contracts.
- Preserve events from every simulated tick. Destruction events may carry invalid historical handles.
- Keep broad-phase acceleration and numerical contact solving in the proven solver.
- No callbacks may mutate a locked solver world. Application callbacks remain synchronous.
- Document CCD and sensor limitations. Do not claim arbitrary tunneling prevention or bitwise determinism.
- Reject expected misuse through Result/Status; assertions enforce programmer invariants.
- Do not swallow failures. Standard allocation/filesystem exceptions may propagate through RAII.
- No raw owning new/delete outside tightly controlled private factories.

## Build, tests and dependencies

- Treat project warnings as errors (/W4 /WX or equivalent).
- Dependencies must earn their place, have compatible licenses, and use pinned releases and hashes.
- Keep redistributed dependency notices in engine/LICENSES and engine/THIRD_PARTY.md.
- Tests must verify physical behavior, lifecycle, invalid inputs and regression cases.
- Required gates: configure, build and CTest for dev; Release and sanitizer gates for physics changes.
- Verify headless builds when changing physics dependencies.
- Run format-check when available and git diff --check.
- Rendering changes need real pixel/readback evidence, not only mocked calls.
- Keep builds green in cohesive units. Fix the cause of a failing test rather than weakening the assertion.
- No empty folders, speculative interfaces, TODO-driven scaffolding or unrelated features.

## Handoff

After meaningful changes update status.md, whats-next.md and any affected API/architecture docs.
Record exact verification results, platform limits and known omissions. Add an ADR for durable
architecture choices. Do not claim completion over a failing required gate.
