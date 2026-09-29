#include "yk/graphics/GameView.hpp"
#include "yk/graphics/PhysicsDebug.hpp"

namespace yk {
Status drawGameView(Renderer &renderer, SceneRenderer &sceneRenderer, GameRuntime &runtime,
                    Rect viewport, const GameViewOptions &options) {
    const Scene &scene = runtime.scene();
    CameraView view{{0.0F, 0.0F}, 18.0F};
    if (const Camera *camera = SceneRenderer::primaryCamera(scene))
        view = camera->view();
    RenderPass world{SceneRenderer::cameraFor(view, viewport.size), viewport,
                     scene.settings.background};
    if (auto status = renderer.beginPass(world); !status)
        return status;
    Status drawn = sceneRenderer.drawWorld(renderer, scene);
    if (drawn && options.colliders)
        drawn = sceneRenderer.drawColliders(renderer, scene);
    if (drawn && options.physicsDebug)
        drawn = drawPhysicsDebug(renderer, runtime.physics(), 1.0F, 950);
    if (auto ended = renderer.endPass(); drawn && !ended)
        drawn = ended;
    if (!drawn)
        return drawn;

    Camera2D screen;
    screen.position = viewport.size * 0.5F;
    if (auto status = renderer.beginPass({screen, viewport, std::nullopt}); !status)
        return status;
    drawn = sceneRenderer.drawUi(renderer, scene, viewport.size, &runtime.blackboard());
    if (drawn && !options.overlay.empty()) {
        constexpr float scale = 2.0F;
        const Vec2 size = BitmapFont::measure(options.overlay, scale);
        const Vec2 origin{viewport.size.x - size.x - 10.0F,
                          viewport.size.y - size.y - 8.0F}; // Bottom right, clear of HUD text.
        drawn = sceneRenderer.font().draw(renderer, options.overlay, origin + Vec2{2, 2}, scale,
                                          {0, 0, 0, 200}, 9000);
        if (drawn)
            drawn = sceneRenderer.font().draw(renderer, options.overlay, origin, scale,
                                              {255, 255, 120, 255}, 9001);
    }
    if (auto ended = renderer.endPass(); drawn && !ended)
        drawn = ended;
    return drawn;
}
} // namespace yk
