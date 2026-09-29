#include "yk/graphics/GameView.hpp"
#include "yk/graphics/PhysicsDebug.hpp"

namespace yk {
namespace {
Status drawAsPlayed(Renderer &renderer, SceneRenderer &sceneRenderer, const Scene &scene,
                    const physics::World *physics, const Blackboard *variables, Rect viewport,
                    const GameViewOptions &options) {
    CameraView view{{0.0F, 0.0F}, 18.0F};
    if (const Camera *camera = SceneRenderer::primaryCamera(scene))
        view = camera->view();
    RenderPass world{SceneRenderer::cameraFor(view, viewport.size), viewport,
                     scene.settings.background, options.target};
    if (auto status = renderer.beginPass(world); !status)
        return status;
    Status drawn = sceneRenderer.drawWorld(renderer, scene);
    if (drawn && options.colliders)
        drawn = sceneRenderer.drawColliders(renderer, scene);
    if (drawn && options.physicsDebug && physics)
        drawn = drawPhysicsDebug(renderer, *physics, 1.0F, 950);
    if (auto ended = renderer.endPass(); drawn && !ended)
        drawn = ended;
    if (!drawn)
        return drawn;

    Camera2D screen;
    screen.position = viewport.size * 0.5F;
    if (auto status = renderer.beginPass({screen, viewport, std::nullopt, options.target}); !status)
        return status;
    drawn = sceneRenderer.drawUi(renderer, scene, viewport.size, variables);
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
} // namespace

Status drawGameView(Renderer &renderer, SceneRenderer &sceneRenderer, GameRuntime &runtime,
                    Rect viewport, const GameViewOptions &options) {
    return drawAsPlayed(renderer, sceneRenderer, runtime.scene(), &runtime.physics(),
                        &runtime.blackboard(), viewport, options);
}

Status drawScenePreview(Renderer &renderer, SceneRenderer &sceneRenderer, const Scene &scene,
                        Rect viewport, const GameViewOptions &options,
                        const Blackboard *variables) {
    return drawAsPlayed(renderer, sceneRenderer, scene, nullptr, variables, viewport, options);
}
} // namespace yk
