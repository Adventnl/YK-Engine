# ADR 0013: Pressure plates are solid pads that carry their load

Date: 2026-09-30. Status: accepted.

## Context

The first plates were a trigger box: an overlap turned them on, and their sprite was tinted or
nudged. That is wrong in every way a player can see. The box is not a surface, so a character
falling onto a plate passes into it; the plate does not move under the character; and because the
zone that switches the plate and the shape that blocks movement were the same thing, a plate could
not be both a floor button and a wide activation area. Everything that looked like a fix in the
demo (a taller box, a lower sprite) would have hidden the problem in one level, not solved it.

## Decision

A plate is two separate things, both data:

- **The pad**: a kinematic body with a solid collider (`PressurePlate.pad`, the plate itself when
  empty). It is a surface like any other: characters and crates land on it, stand on it and are
  carried by it. `Collider.chamfer` gives it a sloped rim that starts below the floor line, so a
  character or a crate can step onto it without meeting a lip.
- **The sensing** (`PressurePlate.sensing`): `Weight` reads the pad's own contacts (something
  solid, resting on top of it: contact normal within 53 degrees of straight up, separation under
  5 cm, a dynamic body that is not static geometry, matching `activatorTags`, at least
  `minimumMass`), `Region` reads a trigger collider (an activation zone independent of what blocks
  movement), and `Auto` picks `Weight` for a plate with a solid pad and no trigger of its own.

While loaded, the pad accelerates (below gravity, so the load never loses contact) to `pressSpeed`
and stops exactly at `pressDepth`; when unloaded (after a 0.06 s grace, so a bouncing crate does not
chatter) it rises the same way. The plate counts as pressed from 85% of the travel and as released
below 70%, so a load resting on the threshold cannot flicker. Motion is a velocity on a kinematic
body, so the physics engine, not the plate, moves whatever stands on it, and the character's own
ground handling ([ADR 0014](0014-characters-ride-the-velocity-of-the-ground-point.md)) keeps its feet
on the surface. The plate publishes `pressed` and `pressAmount` to its animation, so art follows
the motion without knowing about it.

## Consequences

A plate that a character can fall onto, stand on and sink is a prefab of a root with a pad child
and art; the demo's plates are exactly that and its levels contain no plate-specific code. Old
plates (a trigger collider on the entity itself) keep working: `Auto` reads them as `Region`.
Validation says what a badly built plate is missing (no targets, no pad, no solid collider, a pad
that is not kinematic). The alternatives were a dynamic pad on a prismatic joint with a spring (a
real button, but unstable under a stack of crates and hard to stop at an exact depth) and moving
the standing character's position from the plate (not physical, and wrong for crates). The cost of
the chosen design is that a pad is only as good as Box2D's contact reporting: a load is noticed once
it is within the speculative margin (about 2 cm), which the tests account for.
