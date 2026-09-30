# ADR 0012: Programs are applications first: bundles, per-user logs, no terminal

Date: 2026-09-30. Status: accepted.

## Context

The editor and the player were developed as command-line programs: they found their files relative to
the working directory or the source tree, wrote their messages to a terminal and left nothing behind
when they crashed. On a Mac a person double-clicks an app in a disk image: there is no terminal, the
working directory is `/`, the program lives in `Contents/MacOS` while its data is in
`Contents/Resources`, and a failed start that only prints to stderr looks like an app that will not
open.

## Decision

- **Paths are asked of the operating system** (`yk/core/AppPaths.hpp`): the real path of the
  executable, the bundle's `Resources` folder when the program is in `X.app/Contents/MacOS`, and the
  per-user folders by each system's convention. Nothing at run time uses the working directory,
  `argv[0]` or the source tree.
- **Every program run is a `DiagnosticsSession`**: a rotated log file in the per-user log folder, a
  crash report with a stack trace written by an async-signal-safe handler, a running marker that a
  clean exit removes (so the next start can say the last session ended badly), `SIGPIPE`/`SIGHUP`
  ignored. Start-up failures go through `showFatalError` (log, stderr, message box) and a non-zero
  exit. Diagnostics never block a start: an unwritable log folder is a warning.
- **The engine is shipped as `YK Engine.app` in a `.dmg`**: one CMake script assembles the bundle
  from plain file copies (so a test can run it anywhere), a shell script signs and packages it and
  is the place where Developer ID and notarization credentials enter, and a verification script
  exercises the result through Launch Services on a real Mac.
- **Exported games are the same layout with the game's player as the only program**, with the
  project's icon converted to `.icns`, optional ad hoc or Developer ID signing and a `.dmg`; those
  steps run `codesign` and `hdiutil` through an injectable process runner so the exact commands are
  unit-tested off a Mac.
- **Helper processes are owned**: the player the editor starts is tracked, shown in the status bar,
  stopped from the Build menu and ended when the editor quits.
- **The engine does not know the game**: the sample game is located by `YK_DEMO_PROJECT` (any
  checkout, or none) and games can live in their own repositories (`yk new`).

## Consequences

The real-Mac CI job is the arbiter for everything platform-specific: it found the Control/Command
swap in Dear ImGui that the Linux runs could not. Notarization and universal binaries are wired
only as far as credentials allow and are the first thing to run once an Apple Developer account is
available. The Windows crash handler is compiled but untested. The tests that touch these paths
(`diagnostics`, `diagnostics_crash`, `macos_bundle`, `external_project`, `editor_demo_player`) run in
temporary folders with spaces and non-ASCII characters on purpose.
