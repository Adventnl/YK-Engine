# Code Cleanup Report — entire project

## Scorecard

| Category | Rating |
|---|---|
| Architecture & layering | Needs work |
| File size & granularity | Pass |
| DRY & reuse | Pass |
| Hardcoding / magic values | Needs work |
| Types & correctness | Needs work |
| Async & error handling | Pass |
| State management | Pass |
| Performance & complexity | Pass |
| Density & clarity | Needs work |
| Naming & readability | Needs work |
| Security | Pass |
| Tests & quality gates | Needs work |

Overall: The initial repository was a runtime foundation with a sandbox; it did not
meet the requested engine-only scope or provide any working physics subsystem.
This is the pre-remediation scorecard, not a claim about the final verified state.

## Findings

### P0 — Critical

- None found in the inspected existing implementation.

### P1 — High

- Original CMakeLists.txt:54 and games/sandbox/src/main.cpp:1 — the build produces
  a game/sandbox executable. Why: the user requires the engine alone.
  Fix: remove the source, executable, run target and sandbox smoke test.
- Original docs/build-plan.md:5 and docs/engineering-rules.md:8 — scope mandates
  two games and prohibits standalone rigid-body physics. Why: subsequent sessions
  would reintroduce the wrong product. Fix: replace with the authorized engine-only plan.
- Original docs/status.md:151 and engine/include/yk/core/Math.hpp:1 — no physics
  implementation exists. Why: a working physics engine cannot be delivered by the
  current runtime. Fix: implement and test an owned private Box2D subsystem.

### P2 — Medium

- Original CMakeLists.txt:14 — SDL is unconditionally fetched/linked.
  Why: physics cannot build without window/render dependencies.
  Fix: make runtime optional and add a headless preset.
- Original engine/include/yk/core/Application.hpp:23 — Game names the runtime callback
  interface. Why: the core engine API implies a bundled game.
  Fix: rename it to ApplicationLayer and update runtime tests.
- Original docs/, tests/, LICENSES/ — project directories sit outside engine/.
  Why: violates the requested consolidated layout.
  Fix: move them and update CMake, docs, notices and preset output paths.
- Initial new query implementation in engine/src/physics/Queries.cpp — cached solver
  AABBs include speculative padding. Why: queries return false candidates outside actual
  geometry bounds. Fix: compute transformed geometry AABBs; regression test added.
- Initial reverse native shape lookup in engine/src/physics/World.cpp — native 16-bit
  generations can repeat during heavy churn. Why: lookup may point at an old handle.
  Fix: replace reverse mappings and preserve live records during retirement cleanup;
  70,000 create/destroy cycles verify this regression.

### P3 — Nits

- Original README.md:4 and THIRD_PARTY.md:26 — sandbox controls and checker-art
  references are stale after removal. Fix: replace with library usage and current notices.

## Remediation plan

1. Remove game content and consolidate all engine-owned files.
2. Build lifetime-safe physics API over pinned Box2D; isolate optional SDL support.
3. Integrate real collider debug rendering and test physical behavior/ownership.
4. Run Windows build/test/format/sanitizer/headless/native checks and update persistent docs.

Additional joint families, shape casts, controller policy and packaging were deferred
in the original pass. Native macOS verification is covered by the continuation below.
Final commands/results and limitations are recorded in status.md.

Finish: inspected the entire repository → found 0 P0, 3 P1, 5 P2 and 1 P3 findings
→ fixed the listed scope, layout, missing physics, optional-runtime and query/lifetime issues
→ deferred shape casts, controller policy, additional constraints and non-Windows verification
because they require separate scoped work → changed root CMake/presets/README, engine
physics/graphics/runtime APIs and implementations, engine tests and consolidated docs/notices
→ added 104 physics checks and physics-debug pixel checks; retained core/runtime coverage
→ configure/build/ctest presets dev/release/asan passed 3/3 each; headless passed 2/2;
format-check and git diff --check passed; native Direct3D 11 smoke/readback passed
→ limits documented in physics.md/status.md → no unrelated features added.


## Root-layout continuation (2026-09-27)

The original report above records the earlier Windows pass and its then-current
engine/ layout. Commit f20e619 subsequently moved the engine to the root. ADR 0003
records the current paths and supersedes the original directory-placement decision.

The continuation restores CMake project initialization, CTest registration and the
64-bit Windows guard; aligns presets/docs with root paths; fixes native Clang handle
copy warnings; and adds regressions for sensor overlap lifetime, equivalent hinge
reference angles, frame-clock reset and minimize/restore input handling. Native-ID
generation wrapping during pending end events is covered separately from ordinary
stale-handle checks. See status.md for final current-toolchain gates; earlier Windows
results are historical, not a claim that this continuation ran MSVC.
