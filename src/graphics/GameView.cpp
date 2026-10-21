#include "yk/graphics/GameView.hpp"
#include "yk/graphics/PhysicsDebug.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yk {
namespace {
Status drawAsPlayed(Renderer &renderer, SceneRenderer &sceneRenderer, const Scene &scene,
                    const physics::World *physics, const Blackboard *variables, Rect viewport,
                    const GameViewOptions &options, bool interpolate, float alpha, double time) {
    CameraView view{{0.0F, 0.0F}, 18.0F};
    if (const Camera *camera = SceneRenderer::primaryCamera(scene))
        view = interpolate ? camera->viewAt(alpha) : camera->view();
    bool inventoryHud = variables && variables->number("inventory_hud") != 0;
    if (!inventoryHud)
        for (const EntityId id : scene.orderedIds())
            if (const auto *entity = scene.find(id);
                entity && entity->findComponent("InventoryHud"))
                inventoryHud = true;
    Rect worldViewport = viewport;
    if (inventoryHud) {
        const float scale = std::min(viewport.size.x / 1280.0F, viewport.size.y / 720.0F);
        worldViewport.position.y += 40.0F * scale;
        worldViewport.size.y -= 140.0F * scale;
    }
    Rect rasterViewport = worldViewport;
    std::optional<TextureHandle> raster;
    const Camera *primary = SceneRenderer::primaryCamera(scene);
    if (primary && std::isfinite(primary->pixelsPerUnit) && primary->pixelsPerUnit > 0 &&
        view.visibleHeight > 0 && worldViewport.size.x > 0 && worldViewport.size.y > 0) {
        const float density = primary->pixelsPerUnit;
        const float height = std::clamp(std::round(view.visibleHeight * density), 1.0F, 8192.0F);
        const float width = std::clamp(
            std::round(height * worldViewport.size.x / worldViewport.size.y), 1.0F, 8192.0F);
        rasterViewport = {{0, 0}, {width, height}};
        auto surface = sceneRenderer.worldRaster(renderer, rasterViewport.size);
        if (!surface)
            return Error{surface.error()};
        raster = surface.value();
        // Keep camera motion on the same grid as the scenery, without changing simulation state.
        view.position.x = std::round(view.position.x * density) / density;
        view.position.y = std::round(view.position.y * density) / density;
    }
    RenderPass world{SceneRenderer::cameraFor(view, rasterViewport.size), rasterViewport,
                     scene.settings.background, options.target};
    if (raster)
        world.target = raster;
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
    worldView.viewport = rasterViewport.size;
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

    if (raster) {
        Camera2D compositeCamera;
        compositeCamera.position = worldViewport.size * .5F;
        if (auto status =
                renderer.beginPass({compositeCamera, worldViewport, std::nullopt, options.target});
            !status)
            return status;
        Sprite picture;
        picture.texture = *raster;
        picture.size = worldViewport.size;
        picture.transform.position = worldViewport.size * .5F;
        drawn = renderer.submit(picture);
        if (auto ended = renderer.endPass(); drawn && !ended)
            drawn = ended;
        if (!drawn)
            return drawn;
    }

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
        const PauseMenu *menu = nullptr;
        for (const auto id : scene.orderedIds()) {
            const auto *entity = scene.find(id);
            if (entity && entity->activeInHierarchy())
                if (const auto *candidate = entity->get<PauseMenu>();
                    candidate && candidate->enabled) {
                    menu = candidate;
                    break;
                }
        }
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
        const std::string word = menu ? menu->title : "PAUSED";
        const float unit = std::min(viewport.size.x / 1280.0F, viewport.size.y / 720.0F);
        const float scale =
            menu ? 4.0F * unit : std::clamp(std::floor(viewport.size.y / 120.0F), 3.0F, 10.0F);
        const Vec2 size = BitmapFont::measure(word, scale);
        const Vec2 origin = (viewport.size - size) * 0.5F + Vec2{0, menu ? -157.0F * unit : 0};
        if (menu) {
            Sprite panel = dim;
            panel.size = Vec2{500, 480} * unit;
            panel.transform.position = (viewport.size - panel.size) * 0.5F;
            panel.tint = {18, 31, 46, 255};
            panel.layer = 9401;
            if (drawn)
                drawn = renderer.submit(panel);
            const char *labels[]{"RESUME", "RESTART LEVEL", "LEVEL SELECT", "MAIN MENU"};
            for (int row = 0; row < 4 && drawn; ++row) {
                const auto box = PauseMenu::buttonRect(viewport.size, row);
                Sprite button = panel;
                button.size = box.size;
                button.transform.position = box.position;
                button.layer = 9402;
                button.tint =
                    row == menu->selected ? Color{70, 96, 119, 255} : Color{30, 49, 68, 255};
                if (row == menu->armed)
                    button.tint = {90, 116, 139, 255};
                drawn = renderer.submit(button);
                const Vec2 labelSize = BitmapFont::measure(labels[row], 2.5F * unit);
                if (drawn)
                    drawn = sceneRenderer.font().draw(
                        renderer, labels[row], box.position + (box.size - labelSize) * 0.5F,
                        2.5F * unit,
                        row == menu->selected ? Color{245, 204, 125, 255}
                                              : Color{231, 237, 243, 255},
                        9411);
            }
            const std::string hint = "ARROWS + ENTER / CLICK TO CHOOSE\nESC OR P TO RESUME";
            const auto hintSize = BitmapFont::measure(hint, 1.5F * unit);
            if (drawn)
                drawn = sceneRenderer.font().draw(renderer, hint,
                                                  Vec2{(viewport.size.x - hintSize.x) * 0.5F,
                                                       viewport.size.y * 0.5F + 190 * unit},
                                                  1.5F * unit, {173, 196, 216, 255}, 9411);
        }
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
