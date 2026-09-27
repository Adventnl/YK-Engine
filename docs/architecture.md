# Architecture

## Dependency direction and entry point

`games/sandbox` depends on the public `engine/include/yk` API. `yk_engine` is a static
C++20 library with private SDL3 linkage. SDL calls live in platform/graphics implementations;
game code contains no SDL types or scancodes. SDL_Window has a forward declaration
only for Renderer’s private factory. There are no global engine services or mutable singletons.

The console entry point creates Application, then a stack-owned Game implementation.
`Application::run(Game&)` borrows the game synchronously and retains no callbacks.
Application creation and all runtime/rendering/destruction operations must run on the
OS main thread. Debug assertions enforce that run/destruction/rendering stay on the
creation thread. Application is noncopyable, nonmovable and run is single-use.

## Ownership and failure

Application's private implementation owns, in declaration order, SDL lifetime,
window, and Renderer. Reverse destruction releases queued work and all textures,
then the SDL renderer, window, and SDL. Partial SDL/window/renderer initialization
uses the same cleanup path. Another Application is rejected while SDL is initialized;
embedding into an externally owned SDL session is not supported.

Renderer exclusively owns native textures. TextureHandle is non-owning and opaque.
A weak identity token distinguishes renderer lifetimes, even if a new Renderer occupies
the same address. Shared ownership applies only to this tiny identity token, never to
SDL resources or the renderer. Texture slots are append-only and not reused; release
leaves a tombstone, so stale handles never become another resource. Creating many
short-lived textures therefore grows slot metadata until Renderer destruction.

`release()` is allowed only outside an active render frame. Releasing a cached texture
invalidates every alias; it is **not** reference counting. Game/scenes must agree on
ownership before explicitly releasing shared assets. Unreleased textures live until
Application destruction. Future scene resource policy must build on this explicit contract.

Expected SDL/file/rendering failures return `Result<T>`/`Status` with operation context.
Callers propagate errors to the executable boundary, which logs and returns nonzero.
Programmer misuse of Result access and thread-affinity violations assert in Debug.
Invalid ActionBinding construction throws `std::invalid_argument`; allocation/standard
library exceptions can also propagate. The executable catches standard exceptions at
the outer boundary after RAII unwinding. Exceptions are not used for frame control flow.
Logging writes `[level][subsystem] message` to stderr.

## Frame phases and time

1. Clear input transition flags.
2. Poll SDL events, apply key transitions and window focus/minimize state.
3. Sample monotonic FrameClock; call game update with seconds and a const Keyboard snapshot.
4. Begin/clear rendering using the game's camera snapshot.
5. Ask the game to submit world render commands.
6. Sort, draw, optionally capture the bounded run's last frame, then present.
7. Sleep if necessary to limit loop rate to roughly 120 Hz without VSync.

The first delta is zero. Invalid/backward delta becomes zero and long deltas are clamped
to 100 ms. Focus loss/minimization releases all held keys; unfocused/minimized frames
have zero simulation delta. Regaining focus/restoring resets timing to avoid a pause
jump. Updates and rendering continue while unfocused/minimized; this is not a game
pause menu. VSync is attempted and a warning is logged if unsupported. No fixed step exists.
Game update can request exit; window-close/quit exits before another game update.

## Keyboard/action boundary

Key is an engine enum of the currently supported physical keys. One private mapping
converts SDL scancodes. Keyboard preserves held/pressed/released, ignores repeated
down edges, and preserves both edges for a complete tap in one poll. Focus loss clears
held state with release transitions. Events from other windows or while unfocused are ignored.

Game-owned ActionBindings combine multiple keys; alias handover stays held without
false edges. Action transitions describe the combined final held state, with an additional
short-tap edge when previously up. Bindings must be updated once per frame. Separate
binding objects can serve separate local players. No controller/rebinding UI exists.

## Coordinates, rendering and assets

World axes are +X right and +Y down; units are pixels at zoom 1. Camera position is
at the logical viewport center. Zoom is finite/positive. Camera2D is the authoritative
world/screen conversion; no perspective or camera rotation exists.

A Sprite has a texture handle, position/scale/clockwise degree rotation, explicit world
size, fractional anchor, tint, layer and depth. Scale must be positive; negative-scale
flipping is unsupported. Whole textures are drawn; source regions/atlases are not present.
Game code must choose dimensions that preserve its desired aspect ratio.

Queue commands sort by ascending layer, ascending finite depth, then submission
sequence. This provides deterministic painter ordering and an explicit key for future
Y-sort policy; there is no automatic Y-sort behavior. Debug world rectangles share the
queue and camera. Queue capacity is retained between frames; no custom batching exists.

SDL's 2D Render API is the backend, with SDL choosing its native driver (Metal here).
The logical viewport is fixed to configured dimensions (960x540 by default); SDL
handles physical pixels, Retina scaling and letterboxing. Resizing does not alter world
coordinates or camera projection. Diagnostic BMP readback is slow and occurs only
when explicitly requested, before present; it clips to the physical content viewport.

Textures can be uploaded from tightly packed RGBA Color pixels or loaded from BMP.
BMP requests require an absolute path supplied from the caller's stable asset root;
canonical equivalent paths reuse one texture. No implicit working-directory asset
search, manifest, PNG loader, hot reload or filesystem watcher exists. The sandbox
uses an in-memory checker and is independent of working directory.

## Verification boundaries

Unit tests cover input/action transitions, time bounds, normalization and camera math.
Runtime tests use real SDL dummy/software backends, injected platform events, native
resource operations, initialization failures and resized pixel readback. Pixel checks
verify textures, camera zoom/translation, layer/depth/tie ordering and clear color.
CTest also launches the actual sandbox for eight frames. The sanitizer preset instruments
project code; SDL itself is not sanitizer-instrumented. See status for native/manual
verification and platform gaps.
