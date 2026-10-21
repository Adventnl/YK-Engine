#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Exploration.hpp"
#include "yk/runtime/GameSession.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <array>
#include <cmath>

using namespace yk;

int main() {
    ComponentRegistry registry;
    registerStandardComponents(registry);
    auto loaded = Project::load(YK_EXPLORATION_PROJECT_DIR);
    CHECK(loaded);
    if (!loaded)
        return test::finish("castle_walls");
    auto project = loaded.value();
    ProjectAssets assets(project);
    auto load = [&](const std::string &path) -> Result<std::unique_ptr<Scene>> {
        auto file = project.resolve(path);
        if (!file)
            return Error{file.error()};
        return loadScene(file.value(), registry);
    };
    RuntimeOptions options;
    options.layers = project.layers;
    options.inputMap = project.input;
    options.assets = &assets;
    for (const std::string path : {"scenes/courtyard.ykscene", "scenes/level2.ykscene"}) {
        auto scene = load(path);
        CHECK(scene);
        if (!scene)
            continue;
        auto created = GameSession::create(std::move(scene.value()), path, options, load);
        CHECK(created);
        if (!created)
            continue;
        auto session = std::move(created.value());
        auto &world = session->runtime().scene();
        auto *actor = world.findByName("Explorer");
        const auto *feet = actor->get<Collider>();
        const auto start = actor->worldPosition();
        Keyboard keyboard;
        auto tick = [&](int count) {
            for (int i = 0; i < count; ++i) {
                InputFrame frame;
                frame.keyboard = keyboard;
                session->update(1.0 / 60.0, frame);
                keyboard.beginFrame();
            }
        };
        // Keep the shipping collision and controller, while preventing a guard
        // capture or moving resident from interfering with wall contact checks.
        for (auto id : world.orderedIds()) {
            auto *entity = world.find(id);
            if (auto *watch = entity->get<PatrolWatch>())
                watch->enabled = false;
            if (auto *route = entity->get<NpcPath>())
                route->enabled = false;
        }
        tick(3);
        const std::array<Vec2, 4> outward{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
        const std::array<Key, 4> into{{Key::D, Key::A, Key::S, Key::W}};
        for (std::size_t side = 0; side < outward.size(); ++side) {
            const Vec2 normal = outward[side];
            bool tested = false;
            for (auto id : world.orderedIds()) {
                const auto *wall = world.find(id);
                if (!wall->hasTag("wall_solid"))
                    continue;
                const auto *solid = wall->get<Collider>();
                const Vec2 center = wall->worldPosition() + solid->offset;
                const Vec2 edge =
                    center + Vec2{normal.x * solid->size.x / 2, normal.y * solid->size.y / 2};
                const float radius =
                    (std::abs(normal.x) * feet->size.x + std::abs(normal.y) * feet->size.y) / 2;
                const Vec2 contact = edge + normal * radius;
                const Vec2 approach = contact + normal * .6F;
                const Vec2 sweepCenter = (contact + approach) * .5F;
                const Vec2 sweepSize =
                    feet->size + Vec2{std::abs(normal.x) * .6F, std::abs(normal.y) * .6F};
                bool clear = true;
                for (auto otherId : world.orderedIds()) {
                    const auto *other = world.find(otherId);
                    if (other == actor)
                        continue;
                    for (const auto *shape : other->getAll<Collider>()) {
                        if (shape->isTrigger)
                            continue;
                        const Vec2 delta = other->worldPosition() + shape->offset - sweepCenter;
                        const Vec2 extent = (shape->size + sweepSize) * .5F;
                        if (extent.x - std::abs(delta.x) > .001F &&
                            extent.y - std::abs(delta.y) > .001F)
                            clear = false;
                    }
                }
                if (!clear)
                    continue; // Reject internal partition edges and furnished approaches.
                session->runtime().teleport(*actor, approach - feet->offset);
                tick(3);
                keyboard.set(into[side], true);
                tick(70);
                keyboard.set(into[side], false);
                tick(4);
                const Vec2 actual = actor->worldPosition() + feet->offset;
                if (distance(actual, contact) >= 3.F / 35)
                    std::fprintf(stderr,
                                 "%s: wall contact side %zu at %.3f %.3f, expected %.3f %.3f\n",
                                 path.c_str(), side, actual.x, actual.y, contact.x, contact.y);
                CHECK(distance(actual, contact) < 3.F / 35);
                CHECK(actor->get<AnimatedSprite>()->state().starts_with("idle_"));
                tested = true;
                break;
            }
            CHECK(tested);
        }
        session->runtime().teleport(*actor, start);
    }
    return test::finish("castle_walls");
}
