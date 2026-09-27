# What's Next

The engine-only physics pass is implemented and verified on Windows x64. Keep all
work inside engine/ and preserve the SDL-free physics build. Do not restore a sandbox,
games directory, gameplay roadmap or game executable.

Read engineering-rules.md, build-plan.md, status.md, architecture.md and physics.md.
The next useful physics pass should be selected from an actual consumer requirement:

- Shape casts for swept sensors/character movement.
- A fixed-tick callback contract for sustained force/control updates during catch-up,
  with reentrancy and mutation rules proven by tests.
- Additional constraints (prismatic/weld/wheel), only when requested.
- Measured broad-phase/query/interpolation profiling at realistic body counts.
- Native macOS/Linux builds and hands-on desktop runtime verification.
- Engine library packaging/CI without shipping games.

Do not add speculative interfaces or empty subsystem folders. Audio, animation,
tilemaps, scenes and UI need explicit scoped engine milestones and acceptance tests.

Verification remains configure/build/CTest for dev, release, headless and asan,
plus format-check, git diff --check and relevant native renderer readback.
Record exact platform/toolchain/results and update these docs after meaningful work.
