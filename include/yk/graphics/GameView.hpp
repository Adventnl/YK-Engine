#pragma once
#include "yk/graphics/SceneRenderer.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <string>

namespace yk {
struct GameViewOptions {
    bool physicsDebug{false}; // Outlines of the actual physics shapes.
    bool colliders{false};    // Outlines of collider components.
    std::string overlay;      // Small debug text in the corner (empty: none).
};
// Draws a running game the way players see it: the primary camera's world, then screen-space UI,
// into `viewport` (pixels of the current frame). Shared by the standalone player and the editor's
// play mode. The host should also call runtime.setViewportSize(viewport.size) so camera framing
// matches.
Status drawGameView(Renderer &renderer, SceneRenderer &sceneRenderer, GameRuntime &runtime,
                    Rect viewport, const GameViewOptions &options = {});
} // namespace yk
