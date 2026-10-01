# ADR 0024: One character motor; effects speak to it in flags and factors; equipment is drawn as layers

Date: 2026-10-01. Status: accepted.

## Context

A simulation game has dozens of characters that must move, be stunned, be slowed, run out of breath
and look like what they wear, whether a person, a guard's AI, a navigation agent, a cutscene or a
script is deciding where they go. Giving each controller its own movement code makes every one of
those rules appear several times, differently.

## Decision

- **One motor.** `CharacterMotor` turns an intent (a direction, a strength, run or not) into
  velocity, facing and animation parameters; everything that steers a character, including
  `PlayerCharacterController` (named input actions, held or toggled run), only sets the intent.
- **Effects speak to it in names.** A status effect with the flag `no_move` holds the character, with
  `no_sprint` stops it running, and the factor `move.speed` multiplies its speed. These names are the
  contract between a project's data and the engine's character model (`characterflags::`); the motor
  reads them from the character's `StatusEffects`, so a guard is stunned exactly as a player is and
  an item that slows its wearer needs no code. Other systems read other flags and factors the same
  way (`no_act`, `damage.taken`, `perception.visibility`).
- **Running costs stamina, quietly.** The motor spends `runStaminaPerSecond` from a stat of the
  character's `StatSet` and, when it is used up, refuses to run until the stat has recovered to
  `runResumeStamina`. The drain is `quiet` (a new option of `StatSet::add/spend`): thresholds and
  limits still raise their events, but the per-tick `stat.changed` event is left out, because a
  sprint would otherwise raise sixty a second per runner.
- **No damping on a motor-driven body.** The motor sets velocity itself with its own acceleration and
  deceleration; the body damping the walker defaults used to add made a fast character settle below
  its running speed (4.1 instead of 4.6 m/s with the defaults). Adding a `CharacterMotor` now sets
  the body's damping to zero.
- **Equipment is drawn as layers, by ordinary sprites.** `AppearanceLayers` makes one child sprite
  per layer (outfit, hair, hat, held tool) when the game starts and each frame copies the body's
  cell, flip, size, place, tint and draw order onto them, so an animated body animates its layers
  with no renderer change. What a layer shows is the item worn in the character's `Inventory`
  (`equip.appearance`, later slots on top) over the component's defaults; an item names a layer
  with an empty path to hide it (hair under a hood). The layer entities are not part of the scene
  file.

## Alternatives considered

- **The renderer composes layers.** It would couple `SceneRenderer` to items and inventories and
  duplicate the culling and sorting the sprite path already has.
- **Per-controller movement rules.** Rejected for the reason above.
- **Effects naming motor settings directly** (`"walkSpeed": 2`). Flags and factors are meaningful to
  every system (AI, perception, UI) and stack with the existing effect rules; a motor-only key would
  not.

## Consequences

- Layer sheets must be laid out like the body's (same grid): the author's responsibility, shown in the
  component's description. Nothing checks the pixels.
- The flag and factor names are a documented convention; a project that wants a different word
  for them renames the effect, not the engine.
