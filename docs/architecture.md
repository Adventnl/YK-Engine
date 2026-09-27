# Architecture

## Library and dependency direction

`yk::engine` is a C++20 static library. Public headers live in include/yk.
The root CMake file defines the library and optional engine tests. There is no game target.

```text
physics -> core math/results -> private Box2D
platform / graphics / input -> core -> private SDL3
graphics physics-debug adapter -> physics + Renderer
```

Physics has no platform, camera, renderer or application dependency. YK_RUNTIME=OFF
omits SDL and renderer/platform object files. Core/input/math and physics stay usable.
YK_HAS_RUNTIME reports the build capability to consumers. Runtime headers describe
an optional API and must only be used when that capability is enabled.

## Physics ownership

World owns the native world and all bodies, shapes and joints. It is noncopyable and
nonmovable. Destroying it releases native objects and expires its identity token.
Each engine handle contains a weak identity token and a monotonically increasing
64-bit serial. Active records are stored by serial, so native 16-bit slot generation
wrap cannot resurrect engine handles. No raw native IDs appear in the public API.

Bodies track attached shapes/joints. Destroying a body removes those records,
invalidates their handles, and leaves the other attached body alive. Reverse native
shape lookup retains previously stepped identities until the next solver tick so end
events can refer to destroyed shapes. Unstepped shape identities retire immediately.
Creation skips a recycled native ID if its generation collides with a pending historical
identity, keeping both overlap snapshots and end events safe through generation wrap.
Event consumers must call valid() before using a handle.

World calls must stay on its creation thread. Debug assertions enforce this contract;
concurrent access is not supported. Distinct worlds may be used independently, within
Box2D's world limit. There are no global worlds or asynchronous subscriptions.

The broad phase, contact solver, CCD and sleep islands are Box2D responsibilities.
Engine code validates input, adapts handles and coordinates, controls timing, and
copies backend events into engine-owned values. Solver internals are not accessed.

## Timing and events

advance() accepts elapsed seconds, accumulates fixed ticks, clamps frame delays and
caps catch-up work. It reports steps, simulated time, dropped time and interpolation
alpha. Events from all ticks in that call are preserved with tick numbers.
Calling advance with no tick clears the previous call's event list without mutating
the solver. Rejected time leaves the world unchanged.

Before each tick, the engine stores body poses. interpolatedPose blends the previous
and current solver poses using the residual accumulator, choosing the shortest angular
arc. Teleport resets that body's history. Interpolation is deliberately one tick behind.
Physical state queries always return the current solver state.

Contact and sensor arrays are copied immediately after every solver tick, with
historical handles retained through destruction. There are no application callbacks
inside solver execution. Returned event spans last until the next advance or world
destruction. Query results and debug lines are independent value snapshots.

## Runtime and renderer

Application owns SDL lifetime, window and renderer, in that order; destruction
releases renderer/textures, window, then SDL. Partial initialization uses the same RAII
cleanup path. Only one owning Application is supported while SDL is initialized.
All runtime operations run on the OS main thread and the creation thread.

Application::run borrows an ApplicationLayer synchronously and retains no callbacks.
Run is single-use. Phases: input edges reset, platform events, frame clock, layer update,
begin frame, layer submission, sort/draw/capture/present, optional pacing sleep.
The first delta is zero; invalid/backward time is zero; frame delta is capped at 100 ms.
Unfocused/minimized frames have zero delta and release keys; rendering may continue.
The runtime does not automatically advance a physics world.

Renderer owns textures. Non-owning handles validate a weak renderer identity and
append-only index. Explicit release invalidates aliases and is forbidden during a
frame. Metadata grows with texture churn until teardown; there is no shared asset manager.

Sprites sort by layer, depth and submission sequence. Cameras use +X right/+Y down,
world-unit positions, positive zoom, and a fixed logical viewport with letterboxing.
Sprite angles are degrees; physics angles are radians. The debug adapter converts
meters to renderer units explicitly and submits actual shape outlines as line commands.
Camera projection remains in Renderer; physics debug geometry contains only meters.

BMP loads require absolute paths and cache canonical filenames. RGBA upload,
whole-texture sprites, tints, transforms, world rectangles/lines and diagnostic
readback are supported. Atlas regions, PNG loading, animation and tilemaps are absent.

## Errors and validation

Expected invalid configuration, malformed geometry and stale/foreign handles return
Result/Status errors. Result misuse/thread-affinity violations assert in Debug.
Allocation and standard-library exceptions may propagate; callers can catch at their
boundary. Input action-binding misuse throws invalid_argument as before.

Physics boundary values must be finite and have magnitude at most 1,000,000.
Small geometry dimensions must be at least 0.005 meters. Those checks prevent common
solver assertion paths; they are not a claim that extreme scales simulate well.
Use meter-scale geometry and small coordinates, as described in physics.md.
