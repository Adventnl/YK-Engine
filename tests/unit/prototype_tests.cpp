// Plays the checked-in Elemental Prototype project headlessly with a scripted "bot": if the level
// is broken (unreachable exits, a door that never opens, a hazard on the safe path) this fails.
#include "Modules.hpp"
#include "PrototypeGame.hpp"
#include "support/check.hpp"
#include "yk/assets/AssetSource.hpp"
#include "yk/assets/Validation.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/runtime/GameRuntime.hpp"
#include "yk/scene/RegistryDocs.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <cmath>
#include <cstdlib>
#include <functional>

using namespace yk;
using namespace yk::prototype;

namespace {
struct Actor {
    const char *name;
    Key left, right, jump;
};
constexpr Actor fire{"Fire Character", Key::A, Key::D, Key::W};
constexpr Actor water{"Water Character", Key::Left, Key::Right, Key::Up};

struct Game {
    ComponentRegistry registry;
    Project project;
    ProjectAssets assets;
    std::unique_ptr<GameRuntime> runtime;
    Keyboard keyboard;
    std::vector<std::string> events;
    bool trace{std::getenv("YK_TEST_TRACE") != nullptr};

    explicit Game(const std::string &scene = "scenes/test_level.ykscene")
        : project(loadProject()), assets(project) {
        registerAllModules(registry);
        auto loaded = loadScene(project.resolve(scene).value(), registry);
        CHECK(loaded);
        if (!loaded)
            return;
        RuntimeOptions options;
        options.layers = project.layers;
        options.assets = &assets;
        auto created = GameRuntime::create(std::move(loaded.value()), options);
        CHECK(created);
        if (created) {
            runtime = std::move(created.value());
            runtime->events().subscribe(
                "*", [this](const GameEvent &event) { events.push_back(event.name); });
        }
    }
    static Project loadProject() {
        auto loaded = Project::load(std::filesystem::path(YK_SOURCE_DIR) / "projects" /
                                    "elemental-prototype");
        CHECK(loaded);
        return loaded ? loaded.value() : Project{};
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            runtime->stepOnce(keyboard);
            keyboard.beginFrame();
            if (trace && std::getenv("YK_TEST_TRACE")[0] == '2' && runtime->tick() >= 640 &&
                runtime->tick() <= 690) {
                const auto state =
                    runtime->physics().state(*runtime->bodyOf(entity(water.name).id())).value();
                std::fprintf(stderr,
                             "  t=%llu water x=%.3f y=%.3f v=(%.2f,%.2f) grounded=%d keys L%d R%d "
                             "elevator=%.3f\n",
                             static_cast<unsigned long long>(runtime->tick()), pos(water).x,
                             pos(water).y, static_cast<double>(state.linearVelocity.x),
                             static_cast<double>(state.linearVelocity.y), grounded(water) ? 1 : 0,
                             keyboard.state(water.left).held ? 1 : 0,
                             keyboard.state(water.right).held ? 1 : 0,
                             static_cast<double>(entity("Elevator").get<Door>()->openAmount()));
            }
            if (trace && runtime->tick() % 30 == 0)
                std::fprintf(
                    stderr,
                    "t=%4llu fire=(%.2f,%.2f) water=(%.2f,%.2f) elevator=%.2f plates=%d%d\n",
                    static_cast<unsigned long long>(runtime->tick()), pos(fire).x, pos(fire).y,
                    pos(water).x, pos(water).y,
                    static_cast<double>(entity("Elevator").get<Door>()->openAmount()),
                    entity("Ground Plate").get<PressurePlate>()->pressed() ? 1 : 0,
                    entity("Ledge Plate").get<PressurePlate>()->pressed() ? 1 : 0);
        }
    }
    Entity &entity(const char *name) {
        return *runtime->scene().findByName(name);
    }
    Vec2 pos(const Actor &actor) {
        return entity(actor.name).worldPosition();
    }
    bool grounded(const Actor &actor) {
        return entity(actor.name).get<PlatformerController>()->grounded();
    }
    bool alive(const Actor &actor) {
        return entity(actor.name).get<Killable>()->alive();
    }
    void release(const Actor &actor) {
        keyboard.set(actor.left, false);
        keyboard.set(actor.right, false);
    }
    // Steers toward x; true (with keys released) once within `tolerance`.
    bool walkTo(const Actor &actor, float x, float tolerance = 0.2F) {
        const float delta = x - pos(actor).x;
        if (std::fabs(delta) <= tolerance) {
            release(actor);
            return true;
        }
        keyboard.set(delta > 0 ? actor.right : actor.left, true);
        keyboard.set(delta > 0 ? actor.left : actor.right, false);
        return false;
    }
    // Ticks until done() or the limit; false on timeout.
    bool until(int limit, const std::function<bool()> &done) {
        for (int i = 0; i < limit; ++i) {
            if (done())
                return true;
            tick();
        }
        return done();
    }
    // Runs right to `takeoffX`, jumps holding the jump key through the apex, keeps heading right
    // until it lands again.
    bool runAndJump(const Actor &actor, float takeoffX) {
        if (!until(900, [&] {
                return pos(actor).x >= takeoffX || (keyboard.set(actor.right, true), false);
            }))
            return false;
        keyboard.set(actor.jump, true);
        tick(50);
        keyboard.set(actor.jump, false);
        const bool landed = until(180, [&] { return grounded(actor); });
        release(actor);
        return landed;
    }
    bool happened(const std::string &name) const {
        return std::find(events.begin(), events.end(), name) != events.end();
    }
};

void projectIsValid() {
    Game game;
    const auto issues = validateProject(game.project, game.registry);
    for (const ProjectIssue &issue : issues)
        std::fprintf(stderr, "issue: %s: %s\n", issue.path.c_str(), issue.message.c_str());
    CHECK(issues.empty());
    CHECK(game.project.layers.indexOf("Player") >= 0 &&
          !game.project.layers.interacts(2, 2)); // Players pass through each other.
    CHECK(game.project.startScene == "scenes/test_level.ykscene");
}

void levelIsCompletable() {
    Game game;
    if (!game.runtime)
        return;
    GameRuntime &runtime = *game.runtime;
    game.tick(30);
    CHECK(game.grounded(fire) && game.grounded(water));
    CHECK_NEAR(game.pos(fire).x, 2.0, 0.1);
    CHECK_NEAR(game.pos(water).x, 3.6, 0.1);

    // 1. The water character pulls the lever on its way; fire waits by the door.
    CHECK(game.until(600, [&] { return game.walkTo(water, 10.4F) & game.walkTo(fire, 9.0F); }));
    const auto *leverDoor = game.entity("Lever Door").get<Door>();
    CHECK(game.until(
        300, [&] { return leverDoor->openAmount() > 0.99F; })); // Nobody had to touch the door.
    CHECK(game.happened("lever_toggled") && game.happened("door_opened"));
    CHECK(game.entity("Lever").get<Lever>()->on());

    // 2. Both leap the lava pit; fire waits short of the elevator, water boards it.
    CHECK(game.runAndJump(fire, 13.4F));
    CHECK(game.pos(fire).x > 18.0F && game.alive(fire));
    CHECK(game.until(300, [&] { return game.walkTo(fire, 20.6F, 0.15F); }));
    CHECK(game.runAndJump(water, 13.4F));
    CHECK(game.pos(water).x > 18.0F && game.alive(water));
    const auto *elevator = game.entity("Elevator").get<Door>();
    CHECK(game.until(300, [&] { return game.walkTo(water, 25.0F, 0.3F); }));
    CHECK(elevator->openAmount() == 0.0F); // Nothing has pressed a plate yet.

    // 3. Fire crosses the (flush) elevator to the plate beyond it; the elevator lifts water.
    CHECK(game.until(600, [&] { return game.walkTo(fire, 28.5F, 0.15F); }));
    CHECK(game.entity("Ground Plate").get<PressurePlate>()->pressed());
    CHECK(game.until(600, [&] { return elevator->openAmount() > 0.99F; }));
    CHECK(game.grounded(water) &&
          game.pos(water).y < 10.0F); // Water rode it up to the ledge level.
    CHECK(game.until(300, [&] { return game.walkTo(water, 27.4F, 0.15F); }));

    // 4. Fire steps off the plate; the elevator lowers; fire boards; water holds the ledge plate.
    CHECK(game.until(300, [&] { return game.walkTo(fire, 27.2F, 0.15F); }));
    CHECK(game.until(600, [&] { return elevator->openAmount() < 0.01F; }));
    CHECK(game.until(300, [&] { return game.walkTo(fire, 25.0F, 0.3F); }));
    CHECK(game.until(300, [&] { return game.walkTo(water, 28.5F, 0.2F); }));
    CHECK(game.entity("Ledge Plate").get<PressurePlate>()->pressed());
    CHECK(game.until(600, [&] { return elevator->openAmount() > 0.99F; }));
    CHECK(game.pos(fire).y < 10.0F &&
          game.grounded(fire)); // Fire arrives on the ledge, carried by the elevator.
    CHECK(game.alive(fire) && game.alive(water));

    // 5. Along the ledge (picking up gems), over the goo, into the exits.
    CHECK(game.runAndJump(fire, 29.0F));
    CHECK(game.runAndJump(water, 29.0F));
    CHECK(game.alive(fire) && game.alive(water));
    CHECK(game.until(
        600, [&] { return game.walkTo(fire, 34.5F, 0.3F) & game.walkTo(water, 37.5F, 0.3F); }));
    CHECK(game.until(600, [&] { return game.entity("Level Flow").get<LevelFlow>()->completed(); }));
    CHECK(game.happened("level_completed") &&
          runtime.blackboard().text("level_state") == "complete");
    CHECK(runtime.blackboard().text("level_message") == "LEVEL COMPLETE!");

    // Each element collected its own gems on the way; nobody died.
    CHECK(runtime.blackboard().number("fire_gems") == 2.0);
    CHECK(runtime.blackboard().number("water_gems") ==
          2.0); // The bonus gem in the water pool remains.
    CHECK(!runtime.blackboard().has("deaths"));
    CHECK(runtime.time() < 60.0);
    std::fprintf(stderr, "playthrough finished in %.1f simulated seconds\n", runtime.time());
}

void hazardsAreCharacterSpecific() {
    // Water must not wade through lava; fire may. Reverse for the water pool.
    Game game;
    if (!game.runtime)
        return;
    game.tick(5); // Let the spawn points place the characters first.
    game.runtime->teleport(game.entity(water.name), {16.0F, 20.0F}); // Drop into the lava basin.
    CHECK(game.until(120, [&] { return !game.alive(water); }));
    CHECK(game.alive(fire));
    CHECK(game.until(300, [&] { return game.alive(water); })); // Respawns.
    CHECK(game.pos(water).x < 5.0F);
    game.runtime->teleport(game.entity(fire.name), {16.0F, 20.0F});
    game.tick(60);
    CHECK(game.alive(fire) && game.pos(fire).y > 20.0F); // Fire stands in the lava basin, unharmed.
    game.runtime->teleport(game.entity(fire.name), {32.0F, 20.0F});
    CHECK(game.until(120, [&] { return !game.alive(fire); }));
    game.runtime->teleport(game.entity(water.name), {32.0F, 20.0F});
    game.tick(60);
    CHECK(game.alive(water)); // The water basin is safe for water, and holds its bonus gem.
    CHECK(game.runtime->blackboard().number("water_gems") == 1.0);
    CHECK(game.runtime->blackboard().number("deaths") == 2.0);
}

void restartResets() {
    Game game;
    if (!game.runtime)
        return;
    game.tick(30);
    game.keyboard.set(water.right, true);
    game.tick(90);
    CHECK(game.entity("Lever").get<Lever>()->on() || game.pos(water).x > 5.0F);
    game.keyboard.set(water.right, false);
    game.runtime->blackboard().set("fire_gems", 1.0);
    game.keyboard.set(Key::R, true);
    game.tick(1);
    CHECK(game.runtime->tick() <= 1); // Restarted the whole level.
    CHECK(!game.runtime->blackboard().has("fire_gems") ||
          game.runtime->blackboard().number("fire_gems") == 0.0);
    CHECK_NEAR(game.pos(water).x, 3.6, 0.2);
    CHECK(!game.entity("Lever").get<Lever>()->on());
}

void playgroundRuns() {
    Game game("scenes/playground.ykscene");
    if (!game.runtime)
        return;
    game.tick(300);
    CHECK(game.grounded(fire) && game.grounded(water));
    CHECK(game.runtime->physics().stats().bodies > 8);
}

void componentReferenceIsCurrent() {
    // docs/components.md is generated from the registry; it must match what the code registers.
    Game game;
    auto stored = readTextFile(std::filesystem::path(YK_SOURCE_DIR) / "docs" / "components.md");
    CHECK(stored);
    if (stored)
        CHECK(stored.value() == describeRegistryMarkdown(game.registry));
}
} // namespace

int main() {
    projectIsValid();
    levelIsCompletable();
    hazardsAreCharacterSpecific();
    restartResets();
    playgroundRuns();
    componentReferenceIsCurrent();
    return yk::test::finish("prototype");
}
