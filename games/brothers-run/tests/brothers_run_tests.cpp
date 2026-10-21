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
#include <source_location>
#include <string_view>

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
        auto loaded = Project::load(YK_BROTHERS_PROJECT_DIR);
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
    void jump(const Actor &actor, float x, float floor, const char *rider = nullptr,
              float riderOffset = 0) {
        keys.set(actor.jump, true);
        bool airborne = false, landed = false;
        for (int i = 0; i < 240; ++i) {
            if (rider) {
                const auto &platform = entity(rider);
                x = platform.worldPosition().x * 50.0F + riderOffset;
                floor = platform.worldPosition().y * 50.0F - 14.0F;
            }
            steer(actor, x);
            tick();
            if (entity("Level Flow").get<LevelFlow>()->completed()) {
                landed = true; // Reaching the shared exit can complete during the last jump.
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
    template <class Condition>
    void until(Condition condition, int limit = 1500,
               std::source_location call = std::source_location::current()) {
        for (int i = 0; i < limit && !condition(); ++i)
            tick();
        if (!condition())
            std::fprintf(stderr, "Wait timed out at line %u; Ember %.0f,%.0f; Tide %.0f,%.0f\n",
                         call.line(), at(fire).x, feet(fire), at(water).x, feet(water));
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
    for (const auto &[key, path] : {std::pair{Key::Num1, "scenes/level01.ykscene"},
                                    std::pair{Key::Num2, "scenes/level02.ykscene"}}) {
        Game select("scenes/levels.ykscene");
        select.tick(2);
        select.press(key);
        CHECK(select.runtime->sceneChangeRequested() == path);
    }
}

void mouseMenus() {
    for (const Vec2 viewport : {Vec2{1280, 720}, Vec2{1600, 900}}) {
        Game menu("scenes/menu.ykscene");
        menu.runtime->setViewportSize(viewport);
        auto *button = menu.entity("Play Button").get<UiButton>();
        InputFrame input;
        input.pointer = {viewport * 0.5F + Vec2{-238, 66}, {}, true};
        menu.runtime->stepOnce(input);
        CHECK(button->hovered());
        CHECK(menu.runtime->sceneChangeRequested().empty());
        input.pointer.primary = {true, true, false};
        // A press during a render frame with no fixed tick must not be lost.
        menu.runtime->update(0, input);
        input.beginFrame();
        menu.runtime->stepOnce(input);
        CHECK(button->pressed());
        CHECK(menu.runtime->sceneChangeRequested().empty());
        input.pointer.primary = {false, false, true};
        menu.runtime->stepOnce(input);
        CHECK(menu.runtime->sceneChangeRequested() == "scenes/levels.ykscene");
    }
    for (const int number : {1, 2}) {
        Game select("scenes/levels.ykscene");
        InputFrame input;
        input.pointer = {
            {640.0F + (number == 1 ? -246.0F : 246.0F), 380}, {true, false, false}, true};
        select.runtime->stepOnce(input); // Inherited hold must not activate a new scene.
        input.pointer.primary = {false, false, true};
        select.runtime->stepOnce(input);
        CHECK(select.runtime->sceneChangeRequested().empty());
        input.pointer.primary = {true, true, false};
        select.runtime->stepOnce(input);
        input.pointer.position = {0, 0}; // Drag off the card to cancel.
        input.pointer.primary = {false, false, true};
        select.runtime->stepOnce(input);
        CHECK(select.runtime->sceneChangeRequested().empty());
        input.pointer.position = {640.0F + (number == 1 ? -246.0F : 246.0F), 380};
        input.pointer.primary = {true, true, false};
        select.runtime->stepOnce(input);
        input.pointer.primary = {false, false, true};
        select.runtime->stepOnce(input);
        CHECK(select.runtime->sceneChangeRequested() ==
              (number == 1 ? "scenes/level01.ykscene" : "scenes/level02.ykscene"));
    }
}

void solidTerrain() {
    for (const char *file : {"scenes/level01.ykscene", "scenes/level02.ykscene"}) {
        Game game(file);
        int exits = 0;
        for (const auto id : game.runtime->scene().orderedIds()) {
            const auto *entity = game.runtime->scene().find(id);
            if (entity->get<Goal>())
                ++exits;
            for (const auto *collider : entity->getAll<Collider>())
                if (!collider->isTrigger)
                    CHECK(!collider->oneWay);
        }
        CHECK(exits == 1);
        CHECK_NEAR(game.entity("Shared Exit").worldPosition().x,
                   game.entity("Exit Deck").worldPosition().x, 0.001);
        // The visible floor reaches both screen edges, and below the bottom of the view.
        auto &left = game.entity("Left Corner");
        auto &right = game.entity("Right Corner");
        CHECK_NEAR(left.worldPosition().x - left.get<Collider>()->size.x / 2, 0, 0.001);
        CHECK_NEAR(right.worldPosition().x + right.get<Collider>()->size.x / 2, 1672.0 / 50, 0.001);
    }
    Game game("scenes/level01.ykscene");
    game.tick(30);
    game.runtime->teleport(game.entity(water.name), {104.0F / 50, (824.0F - 48) / 50});
    game.runtime->teleport(game.entity(fire.name), {200.0F / 50, (824.0F - 48) / 50});
    game.keys.set(water.jump, true);
    for (int i = 0; i < 90; ++i) {
        game.tick();
        CHECK(game.at(water).y - 48 >= 671.0F); // Head cannot enter the bonus ledge.
    }
    CHECK(game.deaths == 0);
}

void buttonsAndClearance() {
    for (const auto &[file, button] : {std::pair{"scenes/level01.ykscene", "Red Crossing Button"},
                                       std::pair{"scenes/level02.ykscene", "Shuttle Button"}}) {
        Game game(file);
        CHECK(game.entity("Room Background").get<SpriteRenderer>()->texture.path ==
              (std::string_view(file) == "scenes/level02.ykscene"
                   ? "assets/classroom/generated/background.png" : "assets/reference/room.png"));
        auto &base = game.entity(button);
        auto *plate = base.get<PressurePlate>();
        auto *cap = game.runtime->scene().find(plate->pad);
        const Vec2 housing = base.worldPosition(), rest = cap->worldPosition();
        CHECK(cap->get<Collider>() && cap->get<RigidBody>());
        CHECK(plate->sensing == PlateSensing::Weight);
        // Tide starts ahead of Ember, so neither must pass through the other.
        game.tick(30);
        game.walk(water, housing.x * 50.0F);
        game.tick(60);
        CHECK(plate->pressed());
        CHECK(plate->targets.size() == 1);
        auto *target = game.runtime->scene().find(plate->targets.front());
        CHECK(target && target->get<MovingPlatform>());
        CHECK(target->get<MovingPlatform>()->signalActive());
        const Vec2 before = target->worldPosition();
        game.until([&] { return distance(target->worldPosition(), before) > 0.1F; }, 240);
        CHECK_NEAR(base.worldPosition().y, housing.y, 0.001);
        CHECK_NEAR(cap->worldPosition().y - rest.y, plate->pressDepth, 0.015);
        const auto *surface = cap->get<Collider>();
        CHECK_NEAR(game.feet(water),
                   (cap->worldPosition().y + surface->offset.y - surface->size.y / 2) * 50.0F, 2.0);
        // The whole platform AND a standing rider must clear every static solid.
        for (const auto id : game.runtime->scene().orderedIds()) {
            const auto *moving = game.runtime->scene().find(id);
            const auto *mover = moving->get<MovingPlatform>();
            const auto *collider = moving->get<Collider>();
            if (!mover || !collider)
                continue;
            // Current motion may already have started; recover its authored start.
            auto authored = loadScene(game.project.resolve(file).value(), game.registry);
            const Vec2 origin = authored.value()->findByName(moving->name())->worldPosition();
            const float riderHeight = game.entity(water.name).get<Collider>()->size.y;
            const Vec2 lo{std::min(origin.x, origin.x + mover->travel.x) - collider->size.x / 2,
                          std::min(origin.y, origin.y + mover->travel.y) - collider->size.y / 2 -
                              riderHeight};
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

void levelOneRoute(const Actor actor) {
    Game game("scenes/level01.ykscene");
    game.tick(30);
    if (actor.name == fire.name) {
        // Swap starting places so either character can lead the same route.
        const Vec2 start = game.entity(fire.name).worldPosition();
        game.runtime->teleport(game.entity(fire.name), game.entity(water.name).worldPosition());
        game.runtime->teleport(game.entity(water.name), start);
    }
    game.walk(actor, 272);
    game.jump(actor, 656, 824);
    game.walk(actor, 896);
    game.jump(actor, 1280, 824);
    game.jump(actor, 1376, 728);
    game.jump(actor, 1472, 632);
    game.jump(actor, 1568, 536);
    game.jump(actor, 1208, 472);
    game.walk(actor, 1232);
    const bool before = game.entity("Upper Crossing Lever").get<Lever>()->on();
    game.press(actor.use);
    CHECK(game.entity("Upper Crossing Lever").get<Lever>()->on() != before);
    CHECK(game.entity("Upper Crossing Lever").get<SpriteRenderer>()->flipX == !before);
    game.walk(actor, 1136);
    game.jump(actor, 752, 472);
    game.walk(actor, 656);
    game.jump(actor, 560, 376);
    game.jump(actor, 344, 280);
    game.tick(30);
    CHECK(game.entity("Level Flow").get<LevelFlow>()->completed());
    const Actor teammate = actor.name == fire.name ? water : fire;
    CHECK(game.at(teammate).x < 240);
    CHECK_NEAR(game.feet(teammate), 824, 2.0);
    CHECK(game.deaths == 0);
    game.tick(200);
    CHECK(game.runtime->sceneChangeRequested() == "scenes/level02.ykscene");
}

void levelTwoRoute(const Actor actor) {
    Game game("scenes/level02.ykscene");
    game.tick(30);
    if (actor.name == fire.name)
        game.jump(actor, 400, 824); // Jump over the teammate waiting at spawn.
    else
        game.walk(actor, 400);
    game.tick(45);
    game.walk(actor, 480);
    game.until([&] { return game.entity("Long Shuttle").worldPosition().x * 50 < 640; });
    game.jump(actor, 0, 0, "Long Shuttle");
    game.until([&] { return game.at(actor).x > 936; });
    game.jump(actor, 1192, 824);
    game.walk(actor, 1232);
    if (!game.entity("Elevator").get<MovingPlatform>()->signalActive())
        game.press(actor.use);
    game.walk(actor, 1328);
    game.until([&] { return game.entity("Elevator").worldPosition().y * 50 > 837.5F; });
    game.jump(actor, 0, 0, "Elevator");
    game.until([&] { return std::abs(game.feet(actor) - 488) < 0.5F; });
    game.tick(10); // Disembark at the stop, without the lift's upward jump boost.
    game.jump(actor, 1240, 488);
    game.tick(45);
    CHECK(game.entity("Blue Crossing Button").get<PressurePlate>()->pressed());
    CHECK(game.entity("Blue Shuttle").get<MovingPlatform>()->signalActive());
    game.until([&] { return game.entity("Landing Gate").get<Door>()->openAmount() > 0.99F; });
    game.walk(actor, 1176);
    game.until([&] { return game.entity("Blue Shuttle").worldPosition().x * 50 > 1008; });
    game.jump(actor, 0, 0, "Blue Shuttle");
    game.until([&] { return game.at(actor).x < 720; });
    game.jump(actor, 440, 488);
    game.walk(actor, 304);
    game.tick(45);
    CHECK(game.entity("Return Lift Button").get<PressurePlate>()->pressed());
    game.walk(actor, 256);
    game.until([&] { return game.entity("Return Elevator").worldPosition().y * 50 > 495; });
    game.jump(actor, 0, 0, "Return Elevator");
    game.until([&] { return std::abs(game.feet(actor) - 248) < 0.5F; });
    game.tick(10);
    game.jump(actor, 368, 248);
    game.walk(actor, 432);
    game.tick(45);
    CHECK(game.entity("Red Crossing Button").get<PressurePlate>()->pressed());
    CHECK(game.entity("Red Shuttle").get<MovingPlatform>()->signalActive());
    game.walk(actor, 472);
    game.until([&] { return game.entity("Red Shuttle").worldPosition().x * 50 < 640; });
    game.jump(actor, 0, 0, "Red Shuttle");
    game.until([&] { return game.at(actor).x > 772; });
    game.jump(actor, 1016, 248);
    game.walk(actor, 1136);
    game.tick(30);
    CHECK(game.entity("Level Flow").get<LevelFlow>()->completed());
    const Actor teammate = actor.name == fire.name ? water : fire;
    CHECK(game.at(teammate).x < 240);
    CHECK_NEAR(game.feet(teammate), 824, 2.0);
    CHECK(game.deaths == 0);
    game.tick(200);
    CHECK(game.runtime->sceneChangeRequested() == "scenes/menu.ykscene");
}

void levelTwoSharedRide() {
    Game game("scenes/level02.ykscene");
    game.tick(30);
    const Vec2 parked = game.entity("Long Shuttle").worldPosition();
    game.tick(120);
    CHECK(game.entity("Long Shuttle").worldPosition() == parked);
    game.walk(water, 400);
    game.tick(45);
    game.walk(water, 480);
    game.until([&] { return game.entity("Long Shuttle").worldPosition().x * 50 < 640; });
    game.jump(water, 0, 0, "Long Shuttle", 48);
    game.walk(fire, 480);
    game.until([&] { return game.entity("Long Shuttle").worldPosition().x * 50 < 640; });
    game.jump(fire, 0, 0, "Long Shuttle", -48);
    game.until([&] { return game.at(fire).x > 960; });
    CHECK(game.entity("Shuttle Button").get<PressurePlate>()->pressed());
    for (const Actor actor : {fire, water}) {
        CHECK(game.grounded(actor));
        CHECK_NEAR(game.feet(actor), 696, 2.0);
    }
    CHECK(std::abs(game.at(water).x - game.at(fire).x) > 56);
    CHECK(game.deaths == 0);
}
} // namespace

int main() {
    menuFlow();
    mouseMenus();
    solidTerrain();
    buttonsAndClearance();
    for (const Actor actor : {water, fire}) {
        levelOneRoute(actor);
        levelTwoRoute(actor);
    }
    levelTwoSharedRide();
    return yk::test::finish("brothers_run");
}
