// Renders scenes through the real renderer (SDL software backend) and checks actual pixels.
#include "support/check.hpp"
#include "yk/core/Application.hpp"
#include "yk/graphics/SceneRenderer.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>

using namespace yk;

namespace {
constexpr Color leftClear{20, 24, 40, 255};
constexpr Color rightClear{90, 90, 90, 255};

// Draws into a render target, then shows the target as a sprite in the window.
class TargetTestLayer final : public ApplicationLayer {
  public:
    Status initialize(Renderer &renderer) override {
        auto created = renderer.createRenderTarget(64, 32);
        if (!created)
            return Error{created.error()};
        target_ = created.value();
        CHECK(renderer.nativeTexture(target_) != nullptr);
        CHECK(renderer.textureSize(target_).x == 64 && renderer.textureSize(target_).y == 32);
        CHECK(!renderer.createRenderTarget(0, 10) && !renderer.createRenderTarget(10, 20000));
        auto plain = renderer.builtinTexture(BuiltinTexture::White);
        if (!plain)
            return Error{plain.error()};
        white_ = plain.value();
        return success();
    }
    bool update(const FrameContext &) override {
        return true;
    }
    Status render(Renderer &renderer) override {
        RenderPass invalid{Camera2D{}, std::nullopt, std::nullopt, white_};
        CHECK(!renderer.beginPass(invalid)); // Not a render target.
        Camera2D camera;
        camera.position = {32, 16};
        // Inside the target: a blue field with a red square in its left half.
        if (auto status =
                renderer.beginPass({camera, std::nullopt, Color{0, 0, 255, 255}, target_});
            !status)
            return status;
        Sprite square;
        square.texture = white_;
        square.size = {16, 16};
        square.transform.position = {16, 16};
        square.tint = {255, 0, 0, 255};
        if (auto status = renderer.submit(square); !status)
            return status;
        if (auto status = renderer.endPass(); !status)
            return status;
        // In the window: the target drawn at 3x scale with its own top-left at (10, 10).
        Camera2D screen;
        screen.position = renderer.viewport() * 0.5F;
        if (auto status =
                renderer.beginPass({screen, std::nullopt, Color{10, 10, 10, 255}, std::nullopt});
            !status)
            return status;
        Sprite shown;
        shown.texture = target_;
        shown.size = {192, 96};
        shown.anchor = {0, 0};
        shown.transform.position = {10, 10};
        if (auto status = renderer.submit(shown); !status)
            return status;
        return renderer.endPass();
    }

  private:
    TextureHandle target_, white_;
};

class RenderTestLayer final : public ApplicationLayer {
  public:
    Status initialize(Renderer &renderer) override {
        registerEngineComponents(registry_);
        auto created = SceneRenderer::create(renderer, nullptr);
        if (!created)
            return Error{created.error()};
        sceneRenderer_ = std::move(created.value());
        scene_ = std::make_unique<Scene>(registry_, 5);
        const auto sprite = [&](const char *name, Vec2 position, Vec2 size, Color color,
                                int layer = 0) -> Entity & {
            Entity &entity = scene_->createEntity(name);
            entity.transform().position = position;
            auto &component = entity.add<SpriteRenderer>();
            component.size = size;
            component.color = color;
            component.layer = layer;
            return entity;
        };
        sprite("Red", {0, 0}, {2, 2}, {255, 0, 0, 255});
        sprite("Front", {0.9F, 0}, {0.6F, 0.6F}, {255, 255, 0, 255}, 5);
        sprite("Blue", {3, 0}, {2, 2}, {0, 0, 255, 255}).get<SpriteRenderer>()->shape =
            SpriteShape::Ellipse;
        sprite("Green", {-3, 0}, {2, 2}, {0, 255, 0, 255}).transform().rotationDegrees = 45;
        sprite("Hidden", {0, 3}, {2, 2}, {255, 255, 255, 255}).get<SpriteRenderer>()->visible =
            false;
        sprite("Inactive", {0, -3}, {2, 2}, {255, 255, 255, 255}).setActive(false);
        sprite("Mirrored", {-6, 0}, {1, 1}, {255, 128, 0, 255}).transform().scale = {
            -1, 1}; // Must not error.
        Entity &panel = scene_->createEntity("Panel");
        auto &ui = panel.add<UiPanel>();
        ui.anchor = UiAnchor::BottomRight;
        ui.size = {100, 40};
        ui.offset = {10, 10};
        ui.color = {255, 255, 255, 255};
        Entity &letter = scene_->createEntity("Letter");
        auto &text = letter.add<UiText>();
        text.text = "F";
        text.offset = {20, 20};
        text.scale = 3;
        text.shadow = false;
        text.color = {255, 255, 255, 255};
        Entity &label = scene_->createEntity("Label");
        auto &labelText = label.add<UiText>();
        labelText.text = "SCORE {x}\nabc xyz 09";
        labelText.anchor = UiAnchor::BottomLeft;
        labelText.offset = {20, 20};
        labelText.scale = 2;
        variables_.set("x", 42.0);
        return success();
    }
    bool update(const FrameContext &) override {
        return true;
    }
    Status render(Renderer &renderer) override {
        const Vec2 output = renderer.viewport();
        CHECK(output.x == 800 && output.y == 300); // Native resolution: 1 unit = 1 pixel.
        Camera2D left = SceneRenderer::cameraFor({{0, 0}, 10}, {400, 300});
        CHECK_NEAR(left.zoom(), 30.0);
        RenderPass leftPass{left, Rect{{0, 0}, {400, 300}}, leftClear};
        if (auto status = renderer.beginPass(leftPass); !status)
            return status;
        if (auto status = sceneRenderer_->drawWorld(renderer, *scene_); !status)
            return status;
        if (auto status = renderer.endPass(); !status)
            return status;
        RenderPass rightPass{SceneRenderer::cameraFor({{3, 0}, 5}, {400, 300}),
                             Rect{{400, 0}, {400, 300}}, rightClear};
        if (auto status = renderer.beginPass(rightPass); !status)
            return status;
        if (auto status = sceneRenderer_->drawWorld(renderer, *scene_); !status)
            return status;
        CHECK(!renderer.beginPass(rightPass)); // Passes cannot nest.
        if (auto status = renderer.endPass(); !status)
            return status;
        CHECK(!renderer.endPass());
        Camera2D screen;
        screen.position = output * 0.5F;
        RenderPass ui{screen, std::nullopt, std::nullopt};
        if (auto status = renderer.beginPass(ui); !status)
            return status;
        if (auto status = sceneRenderer_->drawUi(renderer, *scene_, output, &variables_); !status)
            return status;
        return renderer.endPass();
    }

  private:
    ComponentRegistry registry_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
    Blackboard variables_;
};

bool pixelIs(SDL_Surface *surface, int x, int y, Color expected, int tolerance = 2) {
    Uint8 r{}, g{}, b{}, a{};
    if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a))
        return false;
    const auto close = [&](Uint8 actual, std::uint8_t wanted) {
        return std::abs(int{actual} - int{wanted}) <= tolerance;
    };
    return close(r, expected.r) && close(g, expected.g) && close(b, expected.b);
}
} // namespace

int main(int argc, char **argv) {
    const bool keep = argc > 1 && std::strcmp(argv[1], "--keep") == 0;
    const std::filesystem::path capture = "scene-render-test.bmp";
    {
        auto app = Application::create({"scene render test", 800, 300, 0, 0});
        CHECK(app);
        if (!app)
            return 1;
        RenderTestLayer layer;
        const auto result = app.value()->run(layer, {2, capture});
        if (!result)
            std::fprintf(stderr, "%s\n", result.error().c_str());
        CHECK(result);
    }
    SDL_Surface *image = SDL_LoadBMP(capture.string().c_str());
    CHECK(image != nullptr);
    if (image) {
        CHECK(image->w == 800 && image->h == 300);
        // Left pass: 30 px per unit, world origin at pixel (200, 150).
        CHECK(pixelIs(image, 185, 150, {255, 0, 0, 255}));   // Red square.
        CHECK(pixelIs(image, 225, 150, {255, 255, 0, 255})); // Higher layer draws over it.
        CHECK(pixelIs(image, 290, 150, {0, 0, 255, 255}));   // Ellipse center.
        CHECK(pixelIs(image, 316, 124, leftClear));        // Ellipse corner is empty: it is round.
        CHECK(pixelIs(image, 148, 150, {0, 255, 0, 255})); // Rotated square reaches 38 px sideways.
        CHECK(pixelIs(image, 200, 240, leftClear)); // Invisible sprite at world y=+3 is not drawn.
        CHECK(pixelIs(image, 200, 60, leftClear));  // Inactive entity at world y=-3 is not drawn.
        CHECK(pixelIs(image, 20, 150,
                      {255, 128, 0, 255})); // Mirrored (negative scale) sprite still draws.
        // Right pass: 60 px per unit, camera at (3,0): world (3,0) is pixel (600, 150).
        CHECK(pixelIs(image, 600, 150, {0, 0, 255, 255}));
        CHECK(pixelIs(image, 440, 150,
                      {255, 0, 0, 255}));           // Red square seen through the other camera.
        CHECK(pixelIs(image, 700, 10, rightClear)); // Pass clear color.
        CHECK(pixelIs(image, 390, 150, leftClear)); // No bleed across the pass boundary.
        // UI: bottom-right white panel at x 690..790, y 250..290.
        CHECK(pixelIs(image, 740, 270, {255, 255, 255, 255}));
        CHECK(pixelIs(image, 680, 270, rightClear));
        // Glyph "F" at (20, 20), scale 3: rows are 3 px tall, columns 3 px wide.
        const auto lit = [&](int row, int column) {
            return pixelIs(image, 20 + column * 3 + 1, 20 + row * 3 + 1, {255, 255, 255, 255});
        };
        CHECK(lit(0, 0) && lit(0, 4)); // Top bar spans the full width...
        CHECK(!lit(6, 4) &&
              !lit(3, 4)); // ...and the right side is open below it (not mirrored/flipped).
        CHECK(lit(3, 3) && lit(3, 0) && lit(6, 0)); // Middle bar and left stem.
        CHECK(!lit(5, 2));                          // Bottom rows of F have only the stem.
        SDL_DestroySurface(image);
    }
    // Render targets: pass into a texture, then display it.
    {
        auto app = Application::create({"target test", 400, 200, 0, 0});
        CHECK(app);
        if (app) {
            TargetTestLayer layer;
            const auto result = app.value()->run(layer, {2, capture});
            if (!result)
                std::fprintf(stderr, "%s\n", result.error().c_str());
            CHECK(result);
        }
        if (SDL_Surface *targetImage = SDL_LoadBMP(capture.string().c_str())) {
            // Target pixel (16,16) is inside the red square, drawn at (10+48, 10+48) = (58, 58).
            CHECK(pixelIs(targetImage, 58, 58, {255, 0, 0, 255}));
            CHECK(pixelIs(targetImage, 10 + 3 * 50, 10 + 3 * 16,
                          {0, 0, 255, 255}));                     // Blue field to its right.
            CHECK(pixelIs(targetImage, 5, 5, {10, 10, 10, 255})); // Window clear color outside.
            SDL_DestroySurface(targetImage);
        } else {
            CHECK(false);
        }
    }
    if (!keep)
        std::filesystem::remove(capture);
    return yk::test::finish("scene_render");
}
