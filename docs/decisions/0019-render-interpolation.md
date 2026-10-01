# ADR 0019: The picture is blended between ticks; the simulation is not

Date: 2026-10-01. Status: accepted.

## Context

The runtime simulates at a fixed 60 Hz and the renderer drew the latest tick, so on 120/144 Hz
screens motion visibly stepped, and on any screen the camera and the characters jittered relative to
each other whenever frame and tick times did not match.

## Decision

Each entity keeps the world transform of the previous and the current tick (`captureRenderState`
at the end of every tick); `GameRuntime::interpolationAlpha()` is the accumulator's fraction of a
tick. The renderer and the camera ask `renderTransform(alpha)`: positions and scales blend
linearly, rotation takes the short way round the 180 degree seam, children blend with their
parent. A jump of more than `interpolationSnapDistance` in one tick, a `teleport()` and a
restart snap instead of blending. Simulation, physics and gameplay never see interpolated values.

## Consequences

Nothing gameplay-related changes; rendering lags the simulation by less than one tick; tools that
draw outside a running game use the plain world transform (an entity with no render state).
`foundation` tests check evenness at 144 Hz, children, teleports, rotation across the seam and
the camera.
