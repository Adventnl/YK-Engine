# Physics API

Include `yk/physics/World.hpp` and link `yk::engine`. Box2D stays private.
The solver is [Box2D 3.1.1](https://github.com/erincatto/box2d/releases/tag/v3.1.1);
its [simulation manual](https://box2d.org/documentation/md_simulation.html) describes
the underlying rigid-body behavior.

## Minimal library usage

```cpp
#include "yk/physics/World.hpp"

auto created = yk::physics::World::create();
if (!created) { /* report created.error() */ return; }
auto world = std::move(created.value());

yk::physics::BodyDef floorDef;
floorDef.type = yk::physics::BodyType::Static;
floorDef.pose.position = {0, 5};
auto floor = world->createBody(floorDef);
if (!floor) { /* report error */ return; }
auto floorShape = world->createShape(floor.value(), yk::physics::Box{{10, 0.5F}});
if (!floorShape) { /* report error */ return; }

auto ball = world->createBody();
if (!ball) { /* report error */ return; }
auto ballShape = world->createShape(ball.value(), yk::physics::Circle{0.5F});
if (!ballShape) { /* report error */ return; }

auto step = world->advance(1.0 / 60.0);
if (!step) { /* report error */ return; }
auto state = world->state(ball.value());
if (!state) { /* report error */ return; }
// state.value().pose is the current physical transform.
```

The physics library has no main loop of its own. Scenes are simulated by `GameRuntime`
([ARCHITECTURE.md](ARCHITECTURE.md)), which owns the `World`, calls `advance` each frame and writes the resulting
poses back to entity transforms; an application that uses physics without scenes owns those calls.

## Units, shapes and body types

Use meters, seconds, kilograms, Newtons, Newton-seconds and radians. Default gravity
is (0, 9.81), with +Y down. Render conversion is explicit; a suggested scale is 100
renderer units per meter. Convert angles to degrees when submitting sprites.

Static bodies stay still and have no simulated velocity. Kinematic bodies follow
specified velocity without gravity. Dynamic bodies respond to gravity, forces,
contacts and constraints. Shapes provide mass through density (kg/m^2).
Use fixedRotation for bodies that should not rotate.

Geometry is local to the body. Box uses half-extents and optional local center/angle;
Circle has radius/center; Capsule has two centers/radius. Polygon accepts 3-8 distinct
convex hull vertices, in any order. Duplicate, interior and collinear points are
rejected; concave shapes should be decomposed into multiple convex shapes on one body.
Segments are two-sided, have no area and are limited to static/kinematic bodies.
Segment sensors are rejected. Minimum radius/half-extent/endpoint separation is 0.005 m.

The numeric boundary accepts finite values of magnitude up to 1,000,000, but useful
solver precision requires much smaller values. Prefer roughly 0.1-10 m dynamic shapes
and world coordinates near the origin. This is not a large-world/floating-origin engine.

## Fixed stepping

Defaults: 60 Hz, 4 solver substeps, 250 ms frame clamp, 8 catch-up ticks per advance.
Config validates fixed steps between 0.0001 and 0.1 seconds, substeps 1-64 and catch-up
budgets 1-1024. maxFrameSeconds must cover a tick and be at most one second.

advance accumulates time and reports droppedSeconds when frame delays or catch-up
budgets discard time. It never feeds arbitrary frame delta directly to the solver.
interpolatedPose provides a display transform from previous/current states and the
remaining fraction. state provides the authoritative simulation transform.

Forces/torques are cleared after each solver tick. Apply a sustained force before
**each fixed tick**, or advance one tick at a time from the host's fixed-update loop.
If advance performs several ticks, a force applied before it affects the first tick.
A force remains pending when no tick is executed. Impulses change velocity immediately.
Off-center force/impulse takes an optional world-space application point.

setPose teleports and resets interpolation. setEnabled(false) removes the body from
simulation/queries and clears native velocity; setEnabled(true) wakes it. Set the
desired velocity after reenabling. setAwake(false) can clear velocity through solver
sleep behavior. Material/shape density is established at creation; runtime mass editing
and type switching are not exposed.

## Filtering, sensors, contacts

CollisionFilter uses 64-bit category/mask bits. Both shapes must accept the other's
category. Equal nonzero groupIndex overrides masks: positive always collides, negative
never collides. Groups must fit a signed 16-bit integer. setFilter updates a live shape.
Normal rigid-body restrictions still apply (static/kinematic pairs do not generate
solid response without a dynamic body).

Sensors report overlap and apply no response. Their density contributes mass unless
explicitly zero. Sensor events are enabled on all created shapes so sensors can see
solid visitors. contacts(body) returns current manifold data, including points,
separation and normal impulses; sensorOverlaps(sensor) returns visitors from the latest
solver tick, excluding destroyed shapes and disabled bodies immediately. A disabled
sensor returns no overlaps. Teleports and filter changes are reflected after the next tick.

events() exposes ContactBegin, ContactEnd, ContactHit, SensorBegin and SensorEnd for
all ticks performed by the latest successful advance. Sensor is first in sensor
events; contact normals point first -> second. ContactBegin is captured before solver
response and may be a speculative contact. Hit events require sufficient approach
speed (the backend default threshold applies). Events contain their simulation tick.

End events can refer to destroyed handles. Compare them with stored handles, but use
valid() before accessing a shape/body. Copy the event values if they must survive the
next advance. There is no subscription or asynchronous callback lifetime to manage.

## Queries, constraints and diagnostics

rayCast(origin, translation) returns the closest hit, surface point, normal and
fraction in [0,1]. Zero translation returns no hit. Rays beginning inside a shape
do not report that shape. Ray casts can hit sensors; use collision categories/masks to distinguish query targets.
queryAabb uses actual transformed shape bounds, without broad-phase padding. It is
a bounds query, not exact polygon-vs-rectangle intersection. queryPoint tests actual
solid geometry. Queries obey QueryFilter; results sort by engine creation serial.
Disabled bodies are excluded. Segment point queries have no solid interior.

Distance joints enforce a rest length, or use springHertz/dampingRatio for a spring.
Revolute joints connect local anchors, with optional angle limits and motor speed/torque.
Reference angles are normalized modulo 2pi to preserve equivalent rotations.
Joint definitions require distinct bodies in this world and at least one dynamic body.
Destroying either body invalidates its attached joint. More joint types are not exposed.

debugLines returns transformed shape outlines in meters. Graphics builds expose
drawPhysicsDebug(renderer, world, unitsPerMeter, layer), called inside a render frame.
Static/kinematic/dynamic/sleeping/sensor geometry uses distinct colors.
The adapter draws collider outlines; it does not drive sprite transforms or simulate. (In a scene,
`GameRuntime` does the transform write-back and `SceneRenderer` draws the sprites.)

## Limits

Continuous collision is enabled by default; bullet bodies add fast-body handling
against dynamic/kinematic bodies. Bullet-vs-bullet CCD and continuously swept sensors
are not guaranteed. Sensors are evaluated at discrete ticks; fast visitors may skip
thin triggers. Ray casts can help for explicit fast-path detection; shape casts and
one-way-platform policy are not exposed. Character movement is not part of this API: the gameplay
library's `PlatformerController` drives a dynamic capsule by velocity and detects ground from the
body's current contacts, on top of it.

The API currently offers two joint families, no serialization, no shape casts, no
custom contact/pre-solve callbacks, and does not synchronize poses with a renderer itself (scene
runtimes write poses back to transforms once per frame, the latest tick without interpolation).
World access is single-threaded. Same-build fixed-step repeatability is tested;
cross-platform bitwise determinism and arbitrary speed/scale correctness are not claimed.
