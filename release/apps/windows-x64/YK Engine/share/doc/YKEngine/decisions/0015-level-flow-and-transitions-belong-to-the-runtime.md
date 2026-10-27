# ADR 0015: Level flow and transitions belong to the runtime, not to a game

Date: 2026-09-30. Status: accepted.

## Context

Finishing a level was mostly implicit: a goal set a variable, a text said "level complete", and
the player reloaded the scene or moved on however the host happened to implement scene changes.
The standalone player and the editor's Play mode each had their own copy of that logic, so they
could disagree; there was no state a game could ask "are we playing, or has the level ended?", no
way to lock the controls while an exit animation played, and no fade between scenes. Because the
engine must not know a game, none of that could simply be written into the demo either.

## Decision

- **`LevelFlow` is a reusable component with an explicit state machine** (intro, playing, complete,
  failed). Its data says what ends a level (`goals`, `restartOnDeath`, `timeLimit`), how long the
  ending takes, what the next scene is and which variables carry over. It publishes its state to the
  `Blackboard` and raises events, so text, sounds and other mechanisms follow it without code.
- **The runtime owns the transition.** `GameRuntime` runs a fade state machine (idle, out, covered,
  in) around restarts and scene changes, exposes `screenFade()` to the renderer and a set of named
  input locks (`lockInput(reason, bool)`). `PlayerInput` honors the locks, so controls stop while the
  level is ending, and the raw input is untouched.
- **`GameSession` is the one place that switches scenes.** The player and the editor's Play mode both
  drive a `GameSession`: it loads the requested scene, carries the kept variables across, starts
  the new scene covered and lets it fade in, and keeps running (and reports through the log) when a
  scene fails to load. There is no second copy of the logic to drift.
- **`Goal` owns the exit.** On completion, whoever stands in an exit walks to its middle and fades
  away over `exitDuration` while the `exiting` animation parameter is set, so art can play a clip
  on it (the demo's characters simply walk in and fade) and the engine never learns what the art is.
- **`EventAction` is the glue between events** ("plate_pressed", "goal_reached", "scene_started",
  "level_completed" ...) and their consequences (a signal that receivers follow, another event,
  switching entities on and off, animation triggers, a variable, a sound, a restart, a scene
  change), with a delay or a repeat interval. Delayed reactions, chains and timers are data.

## Consequences

Levels in any game get intro, time limit, failure, retry, continue and next-scene behaviour by
adding one component; the demo's two rooms use exactly that and nothing of it is written for the
demo. The fade time and whether to fade are host options (`--no-fade` for the player), so tests run
at full speed. What this design does not do: it has no scripting language, so a rule that no
combination of `LevelFlow`, `Goal`, `TriggerZone` and `EventAction` can express is still a C++
component in a game module.
