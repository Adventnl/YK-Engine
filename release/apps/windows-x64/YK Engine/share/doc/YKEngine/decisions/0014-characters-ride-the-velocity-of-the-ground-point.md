# ADR 0014: Characters ride the velocity of the ground point

Date: 2026-09-30. Status: accepted.

## Context

`PlatformerController` moves a dynamic capsule by velocity. Standing on a moving platform, it used
the platform body's *linear* velocity as the frame to walk in. That is right for a platform that
slides, and wrong for everything else: on a rotating platform every point but the pivot moves
differently, on a platform that eases in and out the character lags behind or leaves the surface,
and a door or plate that pushes into a character from the side has no rule for who yields. The
symptoms were sliding riders, riders launched off at the end of a shuttle, and characters pressed
into a wall by a door that then kept closing.

## Decision

The character is carried by what the ground *under its feet* is doing, and mechanisms play fair:

- **Ground point velocity.** The controller finds the ground contact (or the snap ray's hit) and
  asks the physics world for the velocity of that point (`World::pointVelocity`: the body's velocity
  plus its angular velocity crossed with the offset from its center of mass), with a half-tick
  centripetal correction for rotation. It matches that velocity on top of its own walking. Angled
  and rotating surfaces, shuttles, elevators and pads that sink all follow from the same rule.
- **Kinematic surfaces are driven by velocity and ease.** `MovingPlatform`, `Door` and
  `PressurePlate` set velocities (and angular velocities by the shortest angle) instead of
  teleporting, with an acceleration below gravity (8 m/s^2): whatever rides them stays in contact
  when they start downward, and they arrive at their ends exactly.
- **Characters are bullets.** Box2D only sweeps non-bullet dynamic bodies against *static* shapes; a
  character falling onto a kinematic pad at speed would otherwise be inside it before the solver
  notices. `PlatformerController` marks its body as a bullet (`World::setBullet`).
- **Crush protection.** A mechanism that is about to move checks whether its leading face is
  touching a dynamic body that is itself touching something else in the way (`pathBlocked`) and
  stops (`stopWhenBlocked`). Nothing is pushed through a wall, and a released door opens again.

## Consequences

Moving, rotating, tilting and sinking surfaces need no per-surface code in the controller and none
in the demo. Two properties are inherent to the backend and documented rather than hidden: a
contact only exists within the speculative margin, so a fast door can overlap what it crushes by a
few centimeters before it notices (the tests allow for it), and a walkable slope is bounded by the
controller's `maxSlopeDegrees` (55), so a step higher than about 15 cm counts as a wall unless it
has a chamfer. Parenting the character to the platform's transform was rejected: it removes the
character from the physics simulation, breaks with two players and crates on the same surface, and
makes jumping off a moving platform a special case.
