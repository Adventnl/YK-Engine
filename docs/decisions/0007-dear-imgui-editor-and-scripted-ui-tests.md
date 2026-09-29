# ADR 0007: Dear ImGui for the editor, viewports as render targets, UI verified by script

Date: 2026-09-29. Status: accepted.

## Context

The editor needs docking panels, a property inspector that is generated rather than hand-laid, and a
scene view drawn by the engine's own renderer. It must be verifiable in a headless environment
without a person clicking.

## Decision

- **Dear ImGui** (docking branch, pinned by commit, MIT) with its SDL3 and SDL_Renderer backends.
  Immediate mode fits a UI generated from reflection data. Keyboard navigation is off so arrow keys
  and WASD belong to the scene view and the game.
- **Viewports are render targets.** The renderer gained passes that target textures. The scene and
  game views render into textures the engine's renderer draws, displayed in panels with
  `ImGui::Image`; gizmos and overlays are drawn with ImGui's draw list on top, so the engine draws
  the world exactly as the game does and the editor adds only what belongs to editing.
- **Editor logic lives outside the UI** (`editor/core`, no SDL/ImGui) and is unit tested. The UI
  layer is thin.
- **UI scripts.** Widgets register named screen rectangles; `yk_editor --script` drives the real UI
  by pushing SDL events (mouse, keys, text), scrolls widgets into view, captures screenshots and
  asserts on editor state. CTest runs the workflow, the sample play-through and the sample editing
  script under SDL's dummy video driver and software renderer, and the `install` test drives an
  installed copy of the editor the same way.
- **Scripted runs use fixed time.** `--script` implies `--fixed-step`: each editor frame advances
  Play by exactly one 1/60 s tick and the UI's clock by 1/60 s. A first version ran on real time and
  failed intermittently (jump height, distance run) when the machine was busy; with fixed steps the
  three scripts also pass under a load average of eight on four cores.

## Consequences

The editor is verified through the path a user takes, not through mocks, and screenshots of that
path can be inspected. Layout changes rarely break scripts because they use names, not pixels.
ImGui's built-in scalable font is used (no font files to ship); HiDPI scaling is implemented from
SDL's display scale but not verified on a high-DPI display in this environment. Native file dialogs
are not used: project and folder pickers are in-app, which is testable and behaves the same
everywhere.
