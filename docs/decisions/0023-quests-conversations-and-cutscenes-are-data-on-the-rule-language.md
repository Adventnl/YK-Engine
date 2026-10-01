# ADR 0023: Quests, conversations and cutscenes are data on the rule language

Date: 2026-10-01. Status: accepted.

## Context

A simulation game is mostly authored story and procedure: favors with several objectives,
conversations that depend on what the player carries and has done, scripted scenes. These must be
writable without recompiling, checked before anything runs, and must not each invent a way to say "if
this and that, then do this". [ADR 0018](0018-one-condition-and-action-language.md) gave the engine one
condition and action language; this ADR says how the three story systems use it.

## Decision

**Quests** (`sim/Quests.hpp`). A quest is a definition in `GameData` (the `quests` section of any
definition file, or a `.ykquest` file). An objective is complete when a condition holds, when an event
has happened `count` times, or when a rule completes it (`manual`); `after` orders objectives; a quest
completes when its required objectives do (or `"complete": "any"`, or its own condition), fails on a
condition or a time limit, and runs action lists at start, completion and failure. A `QuestLog`
component keeps the state of one character (or of the world: put one on a scenario entity), listens to
events, looks at conditions ten times a second, raises `quest.*` and `objective.completed`, and saves
its state through the component save contract. Conditions see the log's entity as the actor.

**Conversations** (`sim/Dialogue.hpp`). `.ykdialogue` keeps its old linear form and gains a graph:
speakers (a name, a side, portraits by expression), nodes (text with `{variable}` placeholders, actions
that run when it is shown) and a way on, either `choices` (each with a condition, actions, a
destination and an optional `once`), `branch` conditions or `next`. The `Dialogue` component walks a
`DialogueSession`; the renderer draws the portrait on its side and the choices. The file is validated
as a graph (unreachable nodes, dangling references, missing portraits, every rule inside). A choice
marked `once` is remembered in the game's variables, so it needs no state of its own.

**Cutscenes** (`sim/Sequence.hpp`). A `.ykseq` file is a list of **cues** at times on the sequence's
own clock. It is a timeline of *what happens when*, not a track editor.
- Any rule action is a cue (`SetVariable`, `PlaySound`, `EmitEvent`, `StartDialogue`, `GiveItem`...), so
  a cutscene can do anything a rule can, and the validator checks it the same way.
- The cues the player carries out itself are the ones that take time or touch the presentation:
  `Wait`, `WaitForEvent`, `Fade`, `CameraMove`, `CameraRelease`, `MoveEntity`.
- `"wait": true` holds the sequence's clock until that cue is finished; without it timed cues run
  alongside. This is the whole synchronisation model.
- The cue's own time key is `time`, not `at`: `SpawnEntity` already has an `at` argument, and every
  action's arguments sit beside the cue's in one flat object. `time` and `wait` are the two reserved
  keys.
- `MoveEntity` places the entity (and its body, with its velocity cleared) each tick without announcing
  a jump, so the drawn walk is interpolated like any other movement; it does not collide, and it does
  not fight a controller for the velocity. Characters that should pathfind are sent by the AI and
  navigation systems, not by a cutscene.
- The screen fade is its own layer in the runtime (`GameContext::setCinematicFade`; the drawn fade is
  the larger of it and a scene transition's), and the camera can be held and released
  (`Camera::hold/release`), so neither a cutscene nor a script has to fake them with entities.
- A sequence holds an input lock while it plays, raises `sequence.started` and `sequence.finished`
  (with `skipped` and `stopped`), can be skipped by a named input action, and can be skipped or
  stopped from a rule. Skipping applies the end state of every cue that has not run (a fade ends where
  the last fade leaves it, a walk ends at its destination, a conversation is not started or is
  closed); stopping leaves entities where they stand and gives the camera, the input and the screen
  back.

## Alternatives considered

- **A track-based timeline editor with keyframes.** Much heavier to build and to diff, and nothing the
  target game needs: its cutscenes are a handful of cues. A list of cues is readable in a text editor
  and checkable by a validator. A visual editor over the same files remains possible.
- **Cutscenes as scripts.** Lua (a later phase) will be able to start and drive sequences for the
  cases that need real logic, but the common intro, door-opening shot or tutorial moment should not
  need a language.
- **Moving entities with physics velocity.** A controller sets its own velocity later in the tick and
  gravity fights it; setting the pose and clearing the velocity each tick is exact and the walk still
  interpolates.

## Consequences

- There is no sequence editor yet; the Inspector shows a summary (cue count, length, warnings) and
  `yk validate` reports everything, with each problem naming the cue by its place in the file.
- A cue that makes a character *walk somewhere along a path* needs a way for a rule action to say "I
  am still running"; that arrives with the navigation actions of the AI phase and will be a cue
  type, not a change of the file format.
- A sequence is not saved part-way. A game that allows saving during a cutscene loses its position in
  it (it restarts); the input lock normally prevents saving in the middle.
- The three kinds share the `RuleSourceVisitor`, so the validator, the editor and `yk` find every
  condition and action in them without knowing the kind.
