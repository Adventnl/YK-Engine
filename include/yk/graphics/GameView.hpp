#pragma once
#include "yk/graphics/SceneRenderer.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <optional>
#include <string>

namespace yk {
struct GameViewOptions {
    bool physicsDebug{false}; // Outlines of the actual physics shapes.
    bool colliders{false};    // Outlines of collider components.
    std::string overlay;      // Small debug text in the corner (empty: none).
    // How far to cover the picture with black, 0 (not at all) .. 1 (fully): a fade between scenes
    // (GameRuntime::screenFade). Drawn over the world and the UI.
    float fade{0.0F};
    // Dim the picture and say PAUSED in the middle (a game the player has paused).
    bool paused{false};
    // Draw into this render target (from Renderer::createRenderTarget) instead of the frame; the
    // viewport rectangle is then in texture pixels.
    std::optional<TextureHandle> target;
};
// Draws a running game the way players see it: the primary camera's world, then screen-space UI,
// into `viewport` (pixels of the current frame). Shared by the standalone player and the editor's
// play mode. The host should also call runtime.setViewportSize(viewport.size) so camera framing
// matches.
Status drawGameView(Renderer &renderer, SceneRenderer &sceneRenderer, GameRuntime &runtime,
                    Rect viewport, const GameViewOptions &options = {});
// The same picture for a scene that is not running (the editor's game preview): no physics, and UI
// placeholders stay unresolved unless `variables` is given.
Status drawScenePreview(Renderer &renderer, SceneRenderer &sceneRenderer, const Scene &scene,
                        Rect viewport, const GameViewOptions &options = {},
                        const Blackboard *variables = nullptr);
} // namespace yk
