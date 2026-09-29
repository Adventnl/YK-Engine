// A large scene through the real runtime and renderer: 40,000 sprites and 1,500 falling boxes. The
// checks are about counts (nothing is lost, nearly everything off screen is culled, the hierarchy
// order is not rebuilt per call); the measured times are printed, and guarded only by budgets so
// generous that a slow or busy machine still passes.
#include "support/check.hpp"
#include "yk/components/Components.hpp"
#include "yk/core/Application.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/graphics/SceneRenderer.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include <chrono>
#include <cstdio>

using namespace yk;

namespace {
constexpr int columns = 200, rows = 200; // 40,000 sprites, one unit apart.
constexpr int boxes = 1500;

using Clock = std::chrono::steady_clock;
double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::unique_ptr<Scene> buildScene(const ComponentRegistry &registry) {
    auto scene = std::make_unique<Scene>(registry, 7);
    scene->createEntity("Camera").add<Camera>();
    Entity &decor = scene->createEntity("Decor");
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            Entity &tile = scene->createEntity("Tile", decor.id());
            tile.transform().position = {static_cast<float>(x), static_cast<float>(y)};
            auto &sprite = tile.add<SpriteRenderer>();
            sprite.size = {0.9F, 0.9F};
            sprite.color = {static_cast<std::uint8_t>(60 + x % 150),
                            static_cast<std::uint8_t>(60 + y % 150), 120, 255};
        }
    // A floor and two walls the boxes pile up against.
    const auto wall = [&](const char *name, Vec2 at, Vec2 size) {
        Entity &entity = scene->createEntity(name);
        entity.transform().position = at;
        entity.add<RigidBody>().type = RigidBodyType::Static;
        entity.add<Collider>().size = size;
    };
    wall("Floor", {100, 205}, {210, 2});
    wall("Left", {-2, 110}, {2, 200});
    wall("Right", {202, 110}, {2, 200});
    for (int i = 0; i < boxes; ++i) {
        Entity &box = scene->createEntity("Box");
        box.transform().position = {static_cast<float>((i * 37) % 190) + 5.0F,
                                    static_cast<float>(150 + (i * 11) % 50)};
        box.add<RigidBody>().type = RigidBodyType::Dynamic;
        box.add<Collider>().size = {0.8F, 0.8F};
    }
    return scene;
}

// Draws the scene through a camera that sees about 30 x 17 units of the 200 x 200 grid.
class DrawLayer final : public ApplicationLayer {
  public:
    explicit DrawLayer(const Scene &scene) : scene_(scene) {}
    Status initialize(Renderer &renderer) override {
        auto created = SceneRenderer::create(renderer, nullptr);
        if (!created)
            return Error{created.error()};
        sceneRenderer_ = std::move(created.value());
        return success();
    }
    bool update(const FrameContext &) override {
        return true;
    }
    Status render(Renderer &renderer) override {
        const Vec2 size = renderer.viewport();
        WorldView view;
        view.camera = SceneRenderer::cameraFor({{100, 100}, 17}, size);
        view.viewport = size;
        RenderPass pass{view.camera, std::nullopt, Color{10, 10, 10, 255}, std::nullopt};
        if (auto status = renderer.beginPass(pass); !status)
            return status;
        const auto start = Clock::now();
        if (auto status = sceneRenderer_->drawWorld(renderer, scene_, view); !status)
            return status;
        drawMilliseconds += millisecondsSince(start);
        ++frames;
        stats = sceneRenderer_->stats();
        return renderer.endPass();
    }
    double drawMilliseconds{};
    int frames{};
    SceneRenderStats stats;

  private:
    const Scene &scene_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
};
} // namespace

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);

    auto start = Clock::now();
    auto scene = buildScene(registry);
    const double buildMs = millisecondsSince(start);
    const std::size_t total = scene->size();
    CHECK(total == 1 + 1 + columns * rows + 3 + boxes);

    // The hierarchy order is cached: asking a thousand times does not rebuild it.
    start = Clock::now();
    const auto *storage = scene->orderedIds().data();
    for (int i = 0; i < 1000; ++i)
        CHECK(scene->orderedIds().data() == storage);
    const double orderMs = millisecondsSince(start);
    CHECK(scene->orderedIds().size() == total);

    // Simulate three seconds of boxes falling and piling up.
    auto created = GameRuntime::create(std::move(scene));
    CHECK(created);
    if (!created)
        return 1;
    GameRuntime &runtime = *created.value();
    start = Clock::now();
    for (int tick = 0; tick < 180; ++tick)
        runtime.update(1.0 / 60.0, InputFrame{});
    const double simulateMs = millisecondsSince(start);
    const auto physics = runtime.physics().stats();
    CHECK(physics.bodies == static_cast<std::size_t>(boxes + 3)); // Nothing lost or duplicated.
    CHECK(runtime.tick() == 180);
    std::size_t escaped = 0; // A box that fell through the floor or out of the walls.
    runtime.scene().forEach([&](const Entity &entity) {
        if (entity.name() == "Box") {
            const Vec2 at = entity.worldPosition();
            escaped += (at.y > 210.0F || at.x < -1.0F || at.x > 201.0F) ? 1 : 0;
        }
    });
    CHECK(escaped == 0);

    // Draw it through a camera that sees a small part of the grid.
    double drawMs = 0.0;
    SceneRenderStats stats;
    int frames = 0;
    {
        auto app = Application::create({"stress test", 960, 540, 0, 0});
        CHECK(app);
        if (!app)
            return 1;
        DrawLayer layer(runtime.scene());
        const auto result = app.value()->run(layer, {20, {}});
        CHECK(result);
        drawMs = layer.drawMilliseconds;
        frames = layer.frames;
        stats = layer.stats;
    }
    CHECK(frames >= 20);
    CHECK(stats.sprites >= static_cast<std::size_t>(columns * rows));
    // About 30 x 17 of the 40,000 tiles are visible; the rest must be skipped before submission.
    CHECK(stats.culled >= stats.sprites * 95 / 100);
    CHECK(stats.quads < 2500);

    const double tickMs = simulateMs / 180.0;
    const double frameMs = frames > 0 ? drawMs / frames : 0.0;
    std::printf(
        "stress: %zu entities (%d sprites, %d bodies); build %.0f ms; order lookups %.2f ms per "
        "1000; tick %.2f ms; draw %.2f ms per frame, %zu of %zu sprites culled, %zu quads\n",
        total, columns * rows, boxes, buildMs, orderMs, tickMs, frameMs, stats.culled,
        stats.sprites, stats.quads);
    // Budgets are more than ten times what a Debug build measures here: they exist to catch a
    // change that makes the cost grow with the square of the entity count (seconds per frame), not
    // to measure speed. Sanitizer builds are slower still and get more room.
    constexpr double slack = YK_STRESS_SLOWDOWN;
    CHECK(tickMs < 1500.0 * slack);
    CHECK(frameMs < 1500.0 * slack);
    CHECK(orderMs < 1500.0 * slack);
    return yk::test::finish("stress");
}
