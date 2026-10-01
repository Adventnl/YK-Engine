#include "yk/graphics/GameView.hpp"
#include "yk/graphics/PhysicsDebug.hpp"
#include <algorithm>
#include <cstdint>

namespace yk {
namespace {
Status drawAsPlayed(Renderer &renderer, SceneRenderer &sceneRenderer, const Scene &scene,
                    const physics::World *physics, const Blackboard *variables, Rect viewport,
                    const GameViewOptions &options, bool interpolate, float alpha, double time) {
    CameraView view{{0.0F, 0.0F}, 18.0F};
    if (const Camera *camera = SceneRenderer::primaryCamera(scene))
        view = interpolate ? camera->viewAt(alpha) : camera->view();
    RenderPass world{SceneRenderer::cameraFor(view, viewport.size), viewport,
                     scene.settings.background, options.target};
    if (auto status = renderer.beginPass(world); !status)
        return status;
    // Which levels the game view shows: the one the camera looks at, and perhaps those below it.
    std::vector<float> levelAlpha;
    int focusLevel = 0;
    if (const Camera *camera = SceneRenderer::primaryCamera(scene))
        focusLevel = camera->focusLevel();
    if (!scene.settings.levels.empty())
        levelAlpha = levelVisibility(scene.settings.levels, focusLevel);
    WorldView worldView;
    worldView.levelAlpha = levelAlpha.empty() ? nullptr : &levelAlpha;
    worldView.time = time;
    worldView.camera = world.camera;
    worldView.viewport = viewport.size;
    worldView.parallax = true;
    worldView.interpolate = interpolate;
    worldView.alpha = alpha;
    Status drawn = sceneRenderer.drawWorld(renderer, scene, worldView);
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
    if (drawn && options.paused) {
        auto white = renderer.builtinTexture(BuiltinTexture::White);
        if (!white)
            return Error{white.error()};
        Sprite dim;
        dim.texture = white.value();
        dim.size = viewport.size;
        dim.anchor = {0.0F, 0.0F};
        dim.transform.position = {0.0F, 0.0F};
        dim.tint = {0, 0, 0, 130};
        dim.layer = 9400; // Over the HUD, under the fade.
        drawn = renderer.submit(dim);
        const char *word = "PAUSED";
        const float scale = std::clamp(std::floor(viewport.size.y / 120.0F), 3.0F, 10.0F);
        const Vec2 size = BitmapFont::measure(word, scale);
        const Vec2 origin = (viewport.size - size) * 0.5F;
        if (drawn)
            drawn = sceneRenderer.font().draw(renderer, word, origin + Vec2{scale, scale}, scale,
                                              {0, 0, 0, 220}, 9410);
        if (drawn)
            drawn = sceneRenderer.font().draw(renderer, word, origin, scale, {255, 255, 255, 255},
                                              9411);
    }
    if (drawn && options.fade > 0.001F) {
        auto white = renderer.builtinTexture(BuiltinTexture::White);
        if (!white)
            return Error{white.error()};
        Sprite cover;
        cover.texture = white.value();
        cover.size = viewport.size;
        cover.anchor = {0.0F, 0.0F};
        cover.transform.position = {0.0F, 0.0F};
        cover.tint = {0, 0, 0,
                      static_cast<std::uint8_t>(std::clamp(options.fade, 0.0F, 1.0F) * 255.0F)};
        cover.layer = 9500; // Over the HUD text and the debug overlay.
        drawn = renderer.submit(cover);
    }
    if (auto ended = renderer.endPass(); drawn && !ended)
        drawn = ended;
    return drawn;
}
} // namespace

Status drawGameView(Renderer &renderer, SceneRenderer &sceneRenderer, GameRuntime &runtime,
                    Rect viewport, const GameViewOptions &options) {
    GameViewOptions shown = options;
    shown.fade = std::max(options.fade, runtime.screenFade());
    return drawAsPlayed(renderer, sceneRenderer, runtime.scene(), &runtime.physics(),
                        &runtime.blackboard(), viewport, shown, true, runtime.interpolationAlpha(),
                        runtime.time());
}

Status drawScenePreview(Renderer &renderer, SceneRenderer &sceneRenderer, const Scene &scene,
                        Rect viewport, const GameViewOptions &options,
                        const Blackboard *variables) {
    return drawAsPlayed(renderer, sceneRenderer, scene, nullptr, variables, viewport, options,
                        false, 1.0F, 0.0);
}
} // namespace yk
