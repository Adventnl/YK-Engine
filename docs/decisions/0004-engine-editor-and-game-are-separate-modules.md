# ADR 0004: Engine, editor and prototype game are separate modules

Date: 2026-09-29. Status: accepted. Supersedes the product boundary of ADR 0002 ("engine only, no
games") and the "no application or game target" clause of ADR 0003.

## Context

The repository owner asked for a usable engine with an editor and a runtime, validated by a
two-player Fireboy-and-Watergirl-style prototype built entirely from reusable components placed in
the editor. The earlier engine-only rule existed to stop game logic leaking into a library; that
goal remains, but the deliverable is now bigger.

## Decision

One repository, three parts with a one-way dependency direction:

- `yk::engine`: scene model, reflection, standard components, headless runtime, physics, renderer,
  audio, project format. No gameplay and no game rules.
- `yk::gameplay`: reusable mechanics as components (signals, plates, doors, hazards, controller,
  checkpoints, goals, standard collision layers). Depends on the engine only.
- The prototype game module (`game/`, `projects/elemental-prototype/`): only what is specific to
  the prototype (`LevelFlow`, entity templates). Depends on gameplay.
- The editor (`editor/`) and the player (`player/`) are hosts that link the modules they run.

The engine and gameplay libraries never include editor or game headers. The old tile-map
"Elemental Escape" foundation and its `game-foundation.md` were removed: they hard-coded a game
into engine-adjacent code, which this boundary forbids.

## Consequences

The prototype proves that levels are authored from generic parts: the sample project's scenes are
data, and the only prototype-specific C++ is one component and a few templates. A different game
adds its own module beside `game/` and a host that links it. Because there is no script or plugin
loading yet, game-specific components are compiled into the editor and player that use them; see
whats-next.md for the scripting/plugin question.
