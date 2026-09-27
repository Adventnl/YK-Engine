# ADR 0001: SDL3 source pin, 2D backend and renderer-owned textures

Date: 2026-09-27. Status: accepted.

## Context

The engine needs a reproducible Windows/macOS baseline and a small 2D draw path.
A second engine/framework and speculative low-level GPU abstraction would exceed scope.
Texture handles must remain safe across releases and complete renderer recreation.

## Decision

Build SDL3 3.2.28 statically from the official release archive with a SHA-256 pin via
CMake FetchContent. Use SDL's 2D Render API behind the proprietary Renderer; let SDL
choose the native driver. Maintain a fixed logical viewport with letterboxing.

Application owns SDL/window/renderer; renderer exclusively owns textures. Game-facing
handles contain a weak renderer identity and append-only slot index. Resource slots
are not reused. BMP loading caches canonical absolute filenames. Explicit release
invalidates all aliases and is forbidden during frame submission/presentation.

## Consequences

No SDL dynamic library deployment is required. First configure needs network or a
verified same-release source override. Retain SDL and bundled component notices.
The current backend provides sprites without custom shaders, batching, or SDL_GPU-level
rendering. A later backend change must preserve the engine API and coordinate contract.

Identity tokens prevent handles from becoming valid in a replacement renderer. The
append-only metadata tradeoff is acceptable for this foundation but should be revisited
if scene asset churn becomes substantial. Shared asset unloading needs an explicit
scene/asset ownership policy; release is not implicit reference counting.
