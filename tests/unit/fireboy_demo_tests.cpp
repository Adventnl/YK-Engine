// Runs the shipped layouts on real physics, with movement and jumps rather than
// teleporting through obstacles. A screenshot cannot prove that a route works.
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Project.hpp"
#include "yk/gameplay/Gameplay.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace yk;

namespace {
struct Actor {
    const char *name;
    Key left, right, jump, use;
};
constexpr Actor fire{"Ember", Key::A, Key::D, Key::W, Key::S};
constexpr Actor water{"Tide", Key::Left, Key::Right, Key::Up, Key::Down};

struct Game {
    ComponentRegistry registry;
    Project project;
    ProjectAssets assets;
    std::unique_ptr<GameRuntime> runtime;
    Keyboard keys;
    int deaths{};

    static Project loadProject() {
        auto loaded = Project::load(YK_FIREBOY_PROJECT_DIR);
        CHECK(loaded);
        return std::move(loaded.value());
    }
    explicit Game(const char *file) : project(loadProject()), assets(project) {
        registerStandardComponents(registry);
        auto scene = loadScene(project.resolve(file).value(), registry);
        CHECK(scene);
        RuntimeOptions options;
        options.assets = &assets;
        options.layers = project.layers;
        options.inputMap = project.input;
        auto started = GameRuntime::create(std::move(scene.value()), options);
        CHECK(started);
        runtime = std::move(started.value());
        runtime->events().subscribe("entity_died", [this](const GameEvent &) { ++deaths; });
    }
    Entity &entity(const char *name) {
        auto *found = runtime->scene().findByName(name);
        CHECK(found);
        return *found;
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            runtime->stepOnce(keys);
            keys.beginFrame();
        }
    }
    void press(Key key) {
        keys.set(key, true);
        tick();
        keys.set(key, false);
        tick();
    }
    Vec2 at(const Actor &actor) {
        return entity(actor.name).worldPosition() * 50.0F;
    }
    float feet(const Actor &actor) {
        return at(actor).y + 48.0F;
    }
    bool grounded(const Actor &actor) {
        return entity(actor.name).get<PlatformerController>()->grounded();
    }
    void steer(const Actor &actor, float pixels) {
        const auto body = runtime->bodyOf(entity(actor.name).id());
        const auto velocity = runtime->physics().state(*body).value().linearVelocity;
        const float desired = std::clamp((pixels - at(actor).x) * 0.12F, -5.8F, 5.8F);
        keys.set(actor.left, velocity.x > desired + 0.2F);
        keys.set(actor.right, velocity.x < desired - 0.2F);
    }
    void stop(const Actor &actor) {
        keys.set(actor.left, false);
        keys.set(actor.right, false);
        keys.set(actor.jump, false);
        tick(10);
    }
    void walk(const Actor &actor, float x) {
        for (int i = 0; i < 400; ++i) {
            steer(actor, x);
            tick();
            if (std::abs(at(actor).x - x) < 4.0F)
                break;
        }
        stop(actor);
        if (std::abs(at(actor).x - x) >= 12.0F)
            std::fprintf(stderr, "%s walk to %.0f stopped at %.0f,%.0f\n", actor.name, x,
                         at(actor).x, feet(actor));
        CHECK(std::abs(at(actor).x - x) < 12.0F);
    }
    void jump(const Actor &actor, float x, float floor, const char *rider = nullptr) {
        keys.set(actor.jump, true);
        bool airborne = false, landed = false;
        for (int i = 0; i < 240; ++i) {
            if (rider) {
                const auto &platform = entity(rider);
                x = platform.worldPosition().x * 50.0F;
                floor = platform.worldPosition().y * 50.0F - 14.0F;
            }
            steer(actor, x);
            tick();
            if (entity("Level Flow").get<LevelFlow>()->completed()) {
                landed = true; // Entering both doors can complete during the last jump.
                break;
            }
            airborne = airborne || !grounded(actor);
            // A button cap on the destination deck stands up to 15 px above it.
            if (airborne && grounded(actor) && std::abs(feet(actor) - floor) < 20.0F &&
                std::abs(at(actor).x - x) < 55.0F) {
                landed = true;
                break;
            }
        }
        stop(actor);
        if (!landed)
            std::fprintf(stderr, "%s jump to %.0f,%.0f ended %.0f,%.0f grounded %d\n", actor.name,
                         x, floor, at(actor).x, feet(actor), grounded(actor) ? 1 : 0);
        CHECK(landed);
    }
    template <class Condition> void until(Condition condition, int limit = 1500) {
        for (int i = 0; i < limit && !condition(); ++i)
            tick();
        CHECK(condition());
    }
};

void menuFlow() {
    Game menu("scenes/menu.ykscene");
    CHECK(menu.project.startScene == "scenes/menu.ykscene");
    menu.tick(180);
    CHECK(menu.runtime->sceneChangeRequested().empty());
    menu.press(Key::Enter);
    CHECK(menu.runtime->sceneChangeRequested() == "scenes/levels.ykscene");
    Game held("scenes/levels.ykscene");
    held.keys.set(Key::Enter, true);
    held.tick(30);
    CHECK(held.runtime->sceneChangeRequested().empty());
    held.keys.set(Key::Enter, false);
    held.tick();
    held.press(Key::Enter);
    CHECK(held.runtime->sceneChangeRequested() == "scenes/level01.ykscene");
    for (const auto [key, path] : {std::pair{Key::Num1, "scenes/level01.ykscene"},
                                   std::pair{Key::Num2, "scenes/level02.ykscene"}}) {
        Game select("scenes/levels.ykscene");
        select.tick(2);
        select.press(key);
        CHECK(select.runtime->sceneChangeRequested() == path);
    }
}

void buttonsAndClearance() {
    for (const auto [file, button] : {std::pair{"scenes/level01.ykscene", "Red Crossing Button"},
                                      std::pair{"scenes/level02.ykscene", "Shuttle Button"}}) {
        Game game(file);
        CHECK(game.entity("Room Background").get<SpriteRenderer>()->texture.path ==
              "assets/reference/room.png");
        auto &base = game.entity(button);
        auto *plate = base.get<PressurePlate>();
        auto *cap = game.runtime->scene().find(plate->pad);
        const Vec2 housing = base.worldPosition(), rest = cap->worldPosition();
        CHECK(cap->get<Collider>() && cap->get<RigidBody>());
        CHECK(plate->sensing == PlateSensing::Weight);
        // Watergirl starts ahead of Fireboy, so neither must pass through the other.
        game.tick(30);
        game.walk(water, housing.x * 50.0F);
        game.tick(60);
        CHECK(plate->pressed());
        CHECK_NEAR(base.worldPosition().y, housing.y, 0.001);
        CHECK_NEAR(cap->worldPosition().y - rest.y, plate->pressDepth, 0.015);
        const auto *surface = cap->get<Collider>();
        CHECK_NEAR(game.feet(water),
                   (cap->worldPosition().y + surface->offset.y - surface->size.y / 2) * 50.0F, 2.0);
        // Every complete swept platform rectangle must clear every static solid.
        for (const auto id : game.runtime->scene().orderedIds()) {
            const auto *moving = game.runtime->scene().find(id);
            const auto *mover = moving->get<MovingPlatform>();
            const auto *collider = moving->get<Collider>();
            if (!mover || !collider)
                continue;
            // Current motion may already have started; recover its authored start.
            auto authored = loadScene(game.project.resolve(file).value(), game.registry);
            const Vec2 origin = authored.value()->findByName(moving->name())->worldPosition();
            const Vec2 lo{std::min(origin.x, origin.x + mover->travel.x) - collider->size.x / 2,
                          std::min(origin.y, origin.y + mover->travel.y) - collider->size.y / 2};
            const Vec2 hi{std::max(origin.x, origin.x + mover->travel.x) + collider->size.x / 2,
                          std::max(origin.y, origin.y + mover->travel.y) + collider->size.y / 2};
            for (const auto otherId : game.runtime->scene().orderedIds()) {
                const auto *other = game.runtime->scene().find(otherId);
                const auto *solid = other->get<Collider>();
                if (!solid || solid->isTrigger || other->get<RigidBody>())
                    continue;
                const Vec2 center = other->worldPosition() + solid->offset;
                const Vec2 a = center - solid->size * 0.5F;
                const Vec2 b = center + solid->size * 0.5F;
                const bool intersects = lo.x < b.x - 0.001F && hi.x > a.x + 0.001F &&
                                        lo.y < b.y - 0.001F && hi.y > a.y + 0.001F;
                if (intersects)
                    std::fprintf(stderr, "%s sweeps through %s\n", moving->name().c_str(),
                                 other->name().c_str());
                CHECK(!intersects);
            }
        }
    }
}

void levelOneRoute() {
    Game game("scenes/level01.ykscene");
    game.tick(30);
    for (const Actor actor : {water, fire}) {
        game.walk(actor, 272);
        game.jump(actor, 656, 824);
        game.walk(actor, 896);
        game.jump(actor, 1280, 824);
        game.walk(actor, 1496);
        game.jump(actor, 1496, 632);
        game.walk(actor, 1400);
        game.jump(actor, 1208, 472);
        game.walk(actor, 1232);
        game.press(actor.use);
        game.walk(actor, 1136);
        game.jump(actor, 752, 472);
        game.walk(actor, 584);
        game.jump(actor, 584, 280);
        game.jump(actor, actor.name == water.name ? 424.0F : 280.0F, 280);
    }
    game.tick(30);
    CHECK(game.entity("Level Flow").get<LevelFlow>()->completed());
    CHECK(game.deaths == 0);
    game.tick(200);
    CHECK(game.runtime->sceneChangeRequested() == "scenes/level02.ykscene");
}

void levelTwoRoute() {
    Game game("scenes/level02.ykscene");
    game.tick(30);
    for (const Actor actor : {water, fire}) {
        game.walk(actor, 400);
        game.tick(45);
        game.walk(actor, 480);
        game.until([&] { return game.entity("Long Shuttle").worldPosition().x * 50 < 640; });
        game.jump(actor, 0, 0, "Long Shuttle");
        game.until([&] { return game.at(actor).x > 1008; });
        game.jump(actor, 1192, 824);
        game.walk(actor, 1232);
        if (!game.entity("Elevator").get<MovingPlatform>()->signalActive())
            game.press(actor.use);
        game.walk(actor, 1472);
        game.until([&] { return game.entity("Elevator").worldPosition().y * 50 > 685.5F; });
        game.jump(actor, 0, 0, "Elevator");
        game.until([&] { return game.feet(actor) < 412; });
        game.jump(actor, 1240, 408);
        game.tick(45);
        game.walk(actor, 1176);
        game.jump(actor, 824, 408);
        game.walk(actor, 800);
        game.tick(45);
        game.walk(actor, 656);
        game.jump(actor, actor.name == water.name ? 296.0F : 184.0F, 408);
    }
    game.tick(30);
    CHECK(game.entity("Level Flow").get<LevelFlow>()->completed());
    CHECK(game.deaths == 0);
    game.tick(200);
    CHECK(game.runtime->sceneChangeRequested() == "scenes/menu.ykscene");
}
} // namespace

int main() {
    menuFlow();
    buttonsAndClearance();
    levelOneRoute();
    levelTwoRoute();
    return yk::test::finish("fireboy_demo");
}
