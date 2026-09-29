# ADR 0004: Engine, editor and game are separate modules

Date: 2026-09-29. Status: accepted (revised in the "playable engine" pass, see ADR 0008). Supersedes
the product boundary of ADR 0002 ("engine only, no games") and the "no application or game target"
clause of ADR 0003.

## Context

The repository owner asked for a usable engine with an editor and a runtime, validated by a
two-player puzzle-platformer built entirely from reusable components placed in the editor. The
earlier engine-only rule existed to stop game logic leaking into a library; that goal remains, but
the deliverable is bigger.

## Decision

One repository, parts with a one-way dependency direction:

- `yk::engine`: scene model, reflection, standard components, input actions, animation, headless
  runtime, physics, renderer, audio, project format, export. No gameplay and no game rules.
- `yk::gameplay`: reusable mechanics as components (signals, plates, levers, doors, hazards,
  controller, checkpoints, goals, level rules, standard collision layers). Depends on the engine
  only.
- Games: a project folder of data, plus optionally a game module of their own components.
- The editor (`editor/`), the player (`player/`) and the command line (`tools/yk/`) are hosts. They
  register the engine and gameplay components and, for a game with its own components, the game
  module's (ADR 0011).

The engine and gameplay libraries never include editor or game headers.

## Consequences

The demo game proves that levels are authored from generic parts: it has no C++ at all. The only
game-specific things in the repository are its data and the scripts that generate its art.
